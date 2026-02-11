#include "WindowManagerWayland.hh"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <string>
#include <unistd.h>

#include <wayland-client.h>

#include "Log.hh"
#include "xdg-shell-client-protocol.hh"
#include "xdg-output-unstable-v1-client-protocol.hh"

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

    zxdg_output_v1_listener kXdgOutputListener {
        &jwm::WindowManagerWayland::onXdgOutputLogicalPosition,
        &jwm::WindowManagerWayland::onXdgOutputLogicalSize,
        &jwm::WindowManagerWayland::onXdgOutputDone,
        &jwm::WindowManagerWayland::onXdgOutputName,
        &jwm::WindowManagerWayland::onXdgOutputDescription
    };

    xdg_wm_base_listener kXdgWmBaseListener {
        &jwm::WindowManagerWayland::onXdgWmBasePing
    };
}

struct jwm::WaylandOutputState {
    jwm::WindowManagerWayland* manager;
    uint32_t name;
    wl_output* output;
    zxdg_output_v1* xdgOutput = nullptr;
    int32_t x = 0;
    int32_t y = 0;
    int32_t width = 0;
    int32_t height = 0;
    int32_t logicalX = 0;
    int32_t logicalY = 0;
    int32_t logicalWidth = 0;
    int32_t logicalHeight = 0;
    int32_t scale = 1;
    bool hasMode = false;
    bool hasLogicalPosition = false;
    bool hasLogicalSize = false;
    std::string xdgName;
    std::string xdgDescription;
};

jwm::WindowManagerWayland::WindowManagerWayland() {
}

jwm::WindowManagerWayland::~WindowManagerWayland() {
    _cleanup();
}

bool jwm::WindowManagerWayland::_initializeNotifyPipe() {
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

    if (!_initializeNotifyPipe()) {
        _cleanup();
        return false;
    }

    _registry = wl_display_get_registry(_display);
    if (_registry == nullptr) {
        JWM_LOG("Wayland: wl_display_get_registry failed");
        _cleanup();
        return false;
    }

    if (wl_registry_add_listener(_registry, &kRegistryListener, this) != 0) {
        JWM_LOG("Wayland: wl_registry_add_listener failed");
        _cleanup();
        return false;
    }

    if (wl_display_roundtrip(_display) < 0) {
        JWM_LOG("Wayland: initial registry roundtrip failed");
        _cleanup();
        return false;
    }

    // A second roundtrip lets wl_output listeners deliver geometry/mode/scale updates.
    if (wl_display_roundtrip(_display) < 0) {
        JWM_LOG("Wayland: initial output roundtrip failed");
        _cleanup();
        return false;
    }

    if (_compositor == nullptr || _xdgWmBase == nullptr || _shm == nullptr) {
        JWM_LOG("Wayland: required globals missing (wl_compositor, xdg_wm_base, or wl_shm)");
        _cleanup();
        return false;
    }

    return true;
}

void jwm::WindowManagerWayland::_cleanup() {
    _runLoop = false;

    _clearXdgOutputBindings();

    for (auto& outputPair : _outputByName) {
        if (outputPair.second->output != nullptr) {
            wl_output_destroy(outputPair.second->output);
        }
    }
    _outputByName.clear();

    if (_xdgOutputManager != nullptr) {
        zxdg_output_manager_v1_destroy(_xdgOutputManager);
        _xdgOutputManager = nullptr;
    }
    _xdgOutputManagerName = std::numeric_limits<uint32_t>::max();

    if (_xdgWmBase != nullptr) {
        xdg_wm_base_destroy(_xdgWmBase);
        _xdgWmBase = nullptr;
    }
    _xdgWmBaseName = std::numeric_limits<uint32_t>::max();

    if (_shm != nullptr) {
        wl_shm_destroy(_shm);
        _shm = nullptr;
    }
    _shmName = std::numeric_limits<uint32_t>::max();

    if (_compositor != nullptr) {
        wl_compositor_destroy(_compositor);
        _compositor = nullptr;
    }
    _compositorName = std::numeric_limits<uint32_t>::max();
    _compositorVersion = 0;

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

void jwm::WindowManagerWayland::_rebuildScreens() {
    std::vector<ScreenInfoWayland> screens;
    screens.reserve(_outputByName.size());
    for (const auto& outputPair : _outputByName) {
        const WaylandOutputState& output = *outputPair.second;
        if (!output.hasMode || output.width <= 0 || output.height <= 0) {
            continue;
        }

        int32_t boundsX = output.hasLogicalPosition ? output.logicalX : output.x;
        int32_t boundsY = output.hasLogicalPosition ? output.logicalY : output.y;
        int32_t boundsWidth = output.hasLogicalSize ? output.logicalWidth : output.width;
        int32_t boundsHeight = output.hasLogicalSize ? output.logicalHeight : output.height;
        if (boundsWidth <= 0 || boundsHeight <= 0) {
            continue;
        }

        float outputScale = static_cast<float>(output.scale);
        if (output.hasLogicalSize && output.logicalWidth > 0 && output.logicalHeight > 0) {
            float widthScale = static_cast<float>(output.width) / static_cast<float>(output.logicalWidth);
            float heightScale = static_cast<float>(output.height) / static_cast<float>(output.logicalHeight);
            outputScale = std::max(widthScale, heightScale);
        }

        ScreenInfoWayland screenInfo = {
            static_cast<long>(output.name),
            IRect::makeXYWH(boundsX, boundsY, boundsWidth, boundsHeight),
            false,
            outputScale
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

void jwm::WindowManagerWayland::_clearXdgOutputBindings() {
    for (auto& outputPair : _outputByName) {
        WaylandOutputState& outputState = *outputPair.second;
        if (outputState.xdgOutput != nullptr) {
            zxdg_output_v1_destroy(outputState.xdgOutput);
            outputState.xdgOutput = nullptr;
        }
        outputState.hasLogicalPosition = false;
        outputState.hasLogicalSize = false;
        outputState.logicalX = 0;
        outputState.logicalY = 0;
        outputState.logicalWidth = 0;
        outputState.logicalHeight = 0;
        outputState.xdgName.clear();
        outputState.xdgDescription.clear();
    }
}

bool jwm::WindowManagerWayland::_bindXdgOutputManager(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_xdgOutputManager != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 3u);
    _xdgOutputManager = static_cast<zxdg_output_manager_v1*>(wl_registry_bind(registry, name, &zxdg_output_manager_v1_interface, bindVersion));
    if (_xdgOutputManager == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(zxdg_output_manager_v1) failed");
        return false;
    }
    _xdgOutputManagerName = name;

    for (auto& outputPair : _outputByName) {
        _bindXdgOutputForOutput(*outputPair.second);
    }
    return true;
}

void jwm::WindowManagerWayland::_bindXdgOutputForOutput(WaylandOutputState& outputState) {
    if (_xdgOutputManager == nullptr || outputState.output == nullptr || outputState.xdgOutput != nullptr) {
        return;
    }
    outputState.xdgOutput = zxdg_output_manager_v1_get_xdg_output(_xdgOutputManager, outputState.output);
    if (outputState.xdgOutput == nullptr) {
        JWM_LOG("Wayland: zxdg_output_manager_v1_get_xdg_output failed");
        return;
    }
    if (zxdg_output_v1_add_listener(outputState.xdgOutput, &kXdgOutputListener, &outputState) != 0) {
        JWM_LOG("Wayland: zxdg_output_v1_add_listener failed");
        zxdg_output_v1_destroy(outputState.xdgOutput);
        outputState.xdgOutput = nullptr;
    }
}

bool jwm::WindowManagerWayland::_bindCompositor(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_compositor != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 6u);
    _compositor = static_cast<wl_compositor*>(wl_registry_bind(registry, name, &wl_compositor_interface, bindVersion));
    if (_compositor == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wl_compositor) failed");
        return false;
    }
    _compositorName = name;
    _compositorVersion = bindVersion;
    return true;
}

bool jwm::WindowManagerWayland::_bindShm(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_shm != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _shm = static_cast<wl_shm*>(wl_registry_bind(registry, name, &wl_shm_interface, bindVersion));
    if (_shm == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wl_shm) failed");
        return false;
    }
    _shmName = name;
    return true;
}

bool jwm::WindowManagerWayland::_bindXdgWmBase(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_xdgWmBase != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _xdgWmBase = static_cast<xdg_wm_base*>(wl_registry_bind(registry, name, &xdg_wm_base_interface, bindVersion));
    if (_xdgWmBase == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(xdg_wm_base) failed");
        return false;
    }
    _xdgWmBaseName = name;
    if (xdg_wm_base_add_listener(_xdgWmBase, &kXdgWmBaseListener, this) != 0) {
        JWM_LOG("Wayland: xdg_wm_base_add_listener failed");
        xdg_wm_base_destroy(_xdgWmBase);
        _xdgWmBase = nullptr;
        _xdgWmBaseName = std::numeric_limits<uint32_t>::max();
        return false;
    }
    return true;
}

wl_display* jwm::WindowManagerWayland::getDisplay() const {
    return _display;
}

wl_compositor* jwm::WindowManagerWayland::getCompositor() const {
    return _compositor;
}

wl_shm* jwm::WindowManagerWayland::getShm() const {
    return _shm;
}

xdg_wm_base* jwm::WindowManagerWayland::getXdgWmBase() const {
    return _xdgWmBase;
}

uint32_t jwm::WindowManagerWayland::getCompositorVersion() const {
    return _compositorVersion;
}

bool jwm::WindowManagerWayland::isReadyForWindows() const {
    return _display != nullptr && _compositor != nullptr && _xdgWmBase != nullptr && _shm != nullptr;
}

std::vector<jwm::ScreenInfoWayland> jwm::WindowManagerWayland::getScreens() const {
    std::lock_guard<std::mutex> lock(_screensLock);
    return _screens;
}

void jwm::WindowManagerWayland::_notifyLoop() {
    if (_notifyWriteFd < 0) {
        return;
    }

    if (!_notifyPending.exchange(true)) {
        char dummy[1] = {0};
        write(_notifyWriteFd, dummy, 1);
    }
}

void jwm::WindowManagerWayland::_drainNotifyPipe() {
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
    _notifyLoop();
}

void jwm::WindowManagerWayland::_processTasks() {
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
        _cleanup();
        return;
    }

    _runLoop = true;
    while (_runLoop) {
        _processTasks();

        while (wl_display_prepare_read(_display) != 0) {
            if (wl_display_dispatch_pending(_display) < 0) {
                _runLoop = false;
                break;
            }
            _processTasks();
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
            _drainNotifyPipe();
        }
        _notifyPending.store(false);

        if (wl_display_dispatch_pending(_display) < 0) {
            JWM_LOG("Wayland: wl_display_dispatch_pending failed");
            break;
        }
    }

    _processTasks();
    _cleanup();
}

void jwm::WindowManagerWayland::terminate() {
    _runLoop = false;
    _notifyLoop();
}

void jwm::WindowManagerWayland::onRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        manager->_bindCompositor(registry, name, version);
        return;
    }
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        manager->_bindShm(registry, name, version);
        return;
    }
    if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        manager->_bindXdgWmBase(registry, name, version);
        return;
    }
    if (strcmp(interface, zxdg_output_manager_v1_interface.name) == 0) {
        manager->_bindXdgOutputManager(registry, name, version);
        return;
    }
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

    manager->_bindXdgOutputForOutput(*outputState);
    manager->_outputByName[name] = std::move(outputState);
}

void jwm::WindowManagerWayland::onRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (name == manager->_compositorName) {
        if (manager->_compositor != nullptr) {
            wl_compositor_destroy(manager->_compositor);
            manager->_compositor = nullptr;
        }
        manager->_compositorName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_xdgWmBaseName) {
        if (manager->_xdgWmBase != nullptr) {
            xdg_wm_base_destroy(manager->_xdgWmBase);
            manager->_xdgWmBase = nullptr;
        }
        manager->_xdgWmBaseName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_shmName) {
        if (manager->_shm != nullptr) {
            wl_shm_destroy(manager->_shm);
            manager->_shm = nullptr;
        }
        manager->_shmName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_xdgOutputManagerName) {
        manager->_clearXdgOutputBindings();
        if (manager->_xdgOutputManager != nullptr) {
            zxdg_output_manager_v1_destroy(manager->_xdgOutputManager);
            manager->_xdgOutputManager = nullptr;
        }
        manager->_xdgOutputManagerName = std::numeric_limits<uint32_t>::max();
        manager->_rebuildScreens();
        return;
    }
    auto outputIt = manager->_outputByName.find(name);
    if (outputIt == manager->_outputByName.end()) {
        return;
    }
    if (outputIt->second->xdgOutput != nullptr) {
        zxdg_output_v1_destroy(outputIt->second->xdgOutput);
        outputIt->second->xdgOutput = nullptr;
    }
    wl_output_destroy(outputIt->second->output);
    manager->_outputByName.erase(outputIt);
    manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->x = x;
    outputState->y = y;
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0 || !outputState->hasMode) {
        outputState->width = width;
        outputState->height = height;
        outputState->hasMode = true;
    }
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputDone(void* data, wl_output* output) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputScale(void* data, wl_output* output, int32_t factor) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->scale = std::max(1, factor);
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgOutputLogicalPosition(void* data, zxdg_output_v1* xdgOutput, int32_t x, int32_t y) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->logicalX = x;
    outputState->logicalY = y;
    outputState->hasLogicalPosition = true;
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgOutputLogicalSize(void* data, zxdg_output_v1* xdgOutput, int32_t width, int32_t height) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->logicalWidth = width;
    outputState->logicalHeight = height;
    outputState->hasLogicalSize = true;
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgOutputDone(void* data, zxdg_output_v1* xdgOutput) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgOutputName(void* data, zxdg_output_v1* xdgOutput, const char* name) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->xdgName = name != nullptr ? name : "";
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgOutputDescription(void* data, zxdg_output_v1* xdgOutput, const char* description) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->xdgDescription = description != nullptr ? description : "";
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onXdgWmBasePing(void* data, xdg_wm_base* xdgWmBase, uint32_t serial) {
    (void) data;
    xdg_wm_base_pong(xdgWmBase, serial);
}
