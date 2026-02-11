#include "WindowManagerWayland.hh"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <wayland-client.h>

#include "Log.hh"

namespace {
    wl_registry_listener kRegistryListener {
        &jwm::WindowManagerWayland::onRegistryGlobal,
        &jwm::WindowManagerWayland::onRegistryGlobalRemove
    };

    wl_output_listener kOutputListener {
        &jwm::WindowManagerWayland::onOutputGeometry,
        &jwm::WindowManagerWayland::onOutputMode,
        &jwm::WindowManagerWayland::onOutputDone,
        &jwm::WindowManagerWayland::onOutputScale
    };
}

struct jwm::WaylandOutputState {
    jwm::WindowManagerWayland* manager;
    uint32_t name;
    wl_output* output;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t scale = 1;
    bool hasMode = false;
};

jwm::WindowManagerWayland::WindowManagerWayland() {
}

jwm::WindowManagerWayland::~WindowManagerWayland() {
    cleanup();
}

bool jwm::WindowManagerWayland::initializeNotifyPipe() {
    int pipes[2];
    if (pipe(pipes) != 0) {
        JWM_LOG("Wayland: failed to create notify pipe: " << strerror(errno));
        return false;
    }

    _notifyReadFd = pipes[0];
    _notifyWriteFd = pipes[1];
    if (fcntl(_notifyReadFd, F_SETFL, O_NONBLOCK) != 0) {
        JWM_LOG("Wayland: failed to set notify read pipe non-blocking mode: " << strerror(errno));
        return false;
    }
    if (fcntl(_notifyWriteFd, F_SETFL, O_NONBLOCK) != 0) {
        JWM_LOG("Wayland: failed to set notify pipe non-blocking mode: " << strerror(errno));
        return false;
    }
    return true;
}

bool jwm::WindowManagerWayland::connect() {
    _display = wl_display_connect(nullptr);
    if (_display == nullptr) {
        JWM_LOG("Wayland: wl_display_connect failed");
        return false;
    }

    if (!initializeNotifyPipe()) {
        cleanup();
        return false;
    }

    _registry = wl_display_get_registry(_display);
    if (_registry == nullptr) {
        JWM_LOG("Wayland: wl_display_get_registry failed");
        cleanup();
        return false;
    }

    if (wl_registry_add_listener(_registry, &kRegistryListener, this) != 0) {
        JWM_LOG("Wayland: wl_registry_add_listener failed");
        cleanup();
        return false;
    }

    if (wl_display_roundtrip(_display) < 0) {
        JWM_LOG("Wayland: initial registry roundtrip failed");
        cleanup();
        return false;
    }

    // A second roundtrip lets wl_output listeners deliver geometry/mode/scale updates.
    if (wl_display_roundtrip(_display) < 0) {
        JWM_LOG("Wayland: initial output roundtrip failed");
        cleanup();
        return false;
    }

    return true;
}

void jwm::WindowManagerWayland::cleanup() {
    _runLoop = false;

    for (auto& outputPair : _outputByName) {
        if (outputPair.second->output != nullptr) {
            wl_output_destroy(outputPair.second->output);
        }
    }
    _outputByName.clear();

    if (_registry != nullptr) {
        wl_registry_destroy(_registry);
        _registry = nullptr;
    }

    if (_display != nullptr) {
        wl_display_disconnect(_display);
        _display = nullptr;
    }

    if (_notifyReadFd >= 0) {
        close(_notifyReadFd);
        _notifyReadFd = -1;
    }
    if (_notifyWriteFd >= 0) {
        close(_notifyWriteFd);
        _notifyWriteFd = -1;
    }
    _notifyPending.store(false);

    {
        std::lock_guard<std::mutex> screensLock(_screensLock);
        _screens.clear();
    }
    {
        std::lock_guard<std::mutex> queueLock(_taskQueueLock);
        while (!_taskQueue.empty()) {
            _taskQueue.pop();
        }
    }
}

void jwm::WindowManagerWayland::rebuildScreens() {
    std::vector<ScreenInfoWayland> screens;
    screens.reserve(_outputByName.size());
    for (const auto& outputPair : _outputByName) {
        const WaylandOutputState& output = *outputPair.second;
        if (!output.hasMode || output.width <= 0 || output.height <= 0) {
            continue;
        }

        ScreenInfoWayland screenInfo = {
            static_cast<long>(output.name),
            IRect::makeXYWH(output.x, output.y, output.width, output.height),
            false,
            static_cast<float>(output.scale)
        };
        screens.push_back(screenInfo);
    }

    std::sort(screens.begin(), screens.end(), [](const ScreenInfoWayland& a, const ScreenInfoWayland& b) {
        return a.id < b.id;
    });

    if (!screens.empty()) {
        screens.front().isPrimary = true;
    }

    {
        std::lock_guard<std::mutex> lock(_screensLock);
        _screens = std::move(screens);
    }
}

std::vector<jwm::ScreenInfoWayland> jwm::WindowManagerWayland::getScreens() const {
    std::lock_guard<std::mutex> lock(_screensLock);
    return _screens;
}

void jwm::WindowManagerWayland::notifyLoop() {
    if (_notifyWriteFd < 0) {
        return;
    }

    if (!_notifyPending.exchange(true)) {
        char dummy[1] = {0};
        write(_notifyWriteFd, dummy, 1);
    }
}

void jwm::WindowManagerWayland::drainNotifyPipe() {
    char buffer[64];
    while (true) {
        ssize_t readCount = read(_notifyReadFd, buffer, sizeof(buffer));
        if (readCount > 0) {
            continue;
        }
        if (readCount < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        }
        break;
    }
}

void jwm::WindowManagerWayland::enqueueTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(_taskQueueLock);
        _taskQueue.push(std::move(task));
    }
    notifyLoop();
}

void jwm::WindowManagerWayland::processTasks() {
    std::unique_lock<std::mutex> lock(_taskQueueLock);
    while (!_taskQueue.empty()) {
        auto callback = std::move(_taskQueue.front());
        _taskQueue.pop();
        lock.unlock();
        callback();
        lock.lock();
    }
}

void jwm::WindowManagerWayland::runLoop() {
    if (_display == nullptr) {
        return;
    }

    int displayFd = wl_display_get_fd(_display);
    if (displayFd < 0) {
        JWM_LOG("Wayland: wl_display_get_fd failed");
        cleanup();
        return;
    }

    _runLoop = true;
    while (_runLoop) {
        processTasks();

        while (wl_display_prepare_read(_display) != 0) {
            if (wl_display_dispatch_pending(_display) < 0) {
                _runLoop = false;
                break;
            }
            processTasks();
        }
        if (!_runLoop) {
            break;
        }

        if (wl_display_flush(_display) < 0 && errno != EAGAIN) {
            JWM_LOG("Wayland: wl_display_flush failed");
            wl_display_cancel_read(_display);
            break;
        }

        struct pollfd events[2];
        events[0].fd = displayFd;
        events[0].events = POLLIN;
        events[0].revents = 0;
        events[1].fd = _notifyReadFd;
        events[1].events = POLLIN;
        events[1].revents = 0;

        int pollStatus = poll(events, 2, -1);
        if (pollStatus < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(_display);
                continue;
            }
            JWM_LOG("Wayland: poll failed");
            wl_display_cancel_read(_display);
            break;
        }

        bool hasDisplayEvent = (events[0].revents & POLLIN) != 0;
        if (hasDisplayEvent) {
            if (wl_display_read_events(_display) < 0) {
                JWM_LOG("Wayland: wl_display_read_events failed");
                break;
            }
        } else {
            wl_display_cancel_read(_display);
        }

        if ((events[1].revents & POLLIN) != 0) {
            drainNotifyPipe();
        }
        _notifyPending.store(false);

        if (wl_display_dispatch_pending(_display) < 0) {
            JWM_LOG("Wayland: wl_display_dispatch_pending failed");
            break;
        }
    }

    processTasks();
    cleanup();
}

void jwm::WindowManagerWayland::terminate() {
    _runLoop = false;
    notifyLoop();
}

void jwm::WindowManagerWayland::onRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (strcmp(interface, wl_output_interface.name) != 0) {
        return;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 3u);
    wl_output* output = static_cast<wl_output*>(wl_registry_bind(registry, name, &wl_output_interface, bindVersion));
    if (output == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wl_output) failed");
        return;
    }

    std::unique_ptr<WaylandOutputState> outputState(new WaylandOutputState());
    outputState->manager = manager;
    outputState->name = name;
    outputState->output = output;

    if (wl_output_add_listener(output, &kOutputListener, outputState.get()) != 0) {
        JWM_LOG("Wayland: wl_output_add_listener failed");
        wl_output_destroy(output);
        return;
    }

    manager->_outputByName[name] = std::move(outputState);
}

void jwm::WindowManagerWayland::onRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    auto outputIt = manager->_outputByName.find(name);
    if (outputIt == manager->_outputByName.end()) {
        return;
    }
    wl_output_destroy(outputIt->second->output);
    manager->_outputByName.erase(outputIt);
    manager->rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->x = x;
    outputState->y = y;
    outputState->manager->rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0 || !outputState->hasMode) {
        outputState->width = width;
        outputState->height = height;
        outputState->hasMode = true;
    }
    outputState->manager->rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputDone(void* data, wl_output* output) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->manager->rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputScale(void* data, wl_output* output, int32_t factor) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->scale = std::max(1, factor);
    outputState->manager->rebuildScreens();
}
