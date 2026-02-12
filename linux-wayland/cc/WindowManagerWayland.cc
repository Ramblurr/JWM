#include "WindowManagerWayland.hh"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <poll.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include <wayland-cursor.h>
#include <xkbcommon/xkbcommon.h>

#include "AppWayland.hh"
#include "KeyModifier.hh"
#include "KeyWayland.hh"
#include "Log.hh"
#include "MouseButtonWayland.hh"
#include "StringUTF16.hh"
#include "WindowWayland.hh"
#include "impl/JNILocal.hh"
#include "impl/Library.hh"
#include "xdg-activation-v1-client-protocol.hh"
#include "xdg-decoration-unstable-v1-client-protocol.hh"
#include "fractional-scale-v1-client-protocol.hh"
#include "pointer-constraints-unstable-v1-client-protocol.hh"
#include "relative-pointer-unstable-v1-client-protocol.hh"
#include "viewporter-client-protocol.hh"
#include "xdg-output-unstable-v1-client-protocol.hh"
#include "xdg-shell-client-protocol.hh"

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

    wl_seat_listener kSeatListener {
        &jwm::WindowManagerWayland::onSeatCapabilities,
        &jwm::WindowManagerWayland::onSeatName
    };

    wl_pointer_listener kPointerListener {
        &jwm::WindowManagerWayland::onPointerEnter,
        &jwm::WindowManagerWayland::onPointerLeave,
        &jwm::WindowManagerWayland::onPointerMotion,
        &jwm::WindowManagerWayland::onPointerButton,
        &jwm::WindowManagerWayland::onPointerAxis,
        &jwm::WindowManagerWayland::onPointerFrame,
        &jwm::WindowManagerWayland::onPointerAxisSource,
        &jwm::WindowManagerWayland::onPointerAxisStop,
        &jwm::WindowManagerWayland::onPointerAxisDiscrete,
#if defined(WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
        &jwm::WindowManagerWayland::onPointerAxisValue120,
        &jwm::WindowManagerWayland::onPointerAxisRelativeDirection
#endif
    };

    wl_keyboard_listener kKeyboardListener {
        &jwm::WindowManagerWayland::onKeyboardKeymap,
        &jwm::WindowManagerWayland::onKeyboardEnter,
        &jwm::WindowManagerWayland::onKeyboardLeave,
        &jwm::WindowManagerWayland::onKeyboardKey,
        &jwm::WindowManagerWayland::onKeyboardModifiers,
        &jwm::WindowManagerWayland::onKeyboardRepeatInfo
    };

    zwp_relative_pointer_v1_listener kRelativePointerListener {
        &jwm::WindowManagerWayland::onRelativePointerMotion
    };

    zwp_locked_pointer_v1_listener kLockedPointerListener {
        &jwm::WindowManagerWayland::onLockedPointerLocked,
        &jwm::WindowManagerWayland::onLockedPointerUnlocked
    };

    xdg_activation_token_v1_listener kActivationTokenListener {
        &jwm::WindowManagerWayland::onActivationTokenDone
    };

    constexpr xkb_keycode_t kXkbKeycodeOffset = 8;
    constexpr float kPixelsPerScroll = 100.0f;

    std::vector<const char*> cursorNamesForType(jwm::MouseCursor cursorType) {
        switch (cursorType) {
            case jwm::MouseCursor::ARROW: return {"default", "left_ptr"};
            case jwm::MouseCursor::CROSSHAIR: return {"crosshair"};
            case jwm::MouseCursor::HELP: return {"help", "question_arrow"};
            case jwm::MouseCursor::POINTING_HAND: return {"pointer", "hand2"};
            case jwm::MouseCursor::IBEAM: return {"text", "xterm"};
            case jwm::MouseCursor::NOT_ALLOWED: return {"not-allowed", "crossed_circle"};
            case jwm::MouseCursor::WAIT: return {"wait", "watch"};
            case jwm::MouseCursor::WIN_UPARROW: return {"up_arrow"};
            case jwm::MouseCursor::RESIZE_NS: return {"ns-resize", "v_double_arrow"};
            case jwm::MouseCursor::RESIZE_WE: return {"ew-resize", "h_double_arrow"};
            case jwm::MouseCursor::RESIZE_NESW: return {"nesw-resize", "size_bdiag"};
            case jwm::MouseCursor::RESIZE_NWSE: return {"nwse-resize", "size_fdiag"};
            default: return {"default", "left_ptr"};
        }
    }

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

    _xkbContext = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    if (_xkbContext == nullptr) {
        JWM_LOG("Wayland: xkb_context_new failed");
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

void jwm::WindowManagerWayland::_resetPointer() {
    _destroyLockedPointer();
    _destroyRelativePointer();
    _pointerLockWindow = nullptr;
    _isPointerLockActive = false;
    _pointerFocusWindow = nullptr;
    _pointerEnterSerial = 0;
    _pointerContentX = 0;
    _pointerContentY = 0;
    _pointerButtonMask = 0;
    _pointerAxisPending = false;
    _pointerAxisDiscretePending = false;
    _pointerAxisValue120Pending = false;
    _pointerAxisX = 0.0;
    _pointerAxisY = 0.0;
    _pointerAxisDiscreteX = 0.0;
    _pointerAxisDiscreteY = 0.0;
    _pointerAxisValue120X = 0.0;
    _pointerAxisValue120Y = 0.0;
    _pointerButtonMask = 0;

    if (_cursorSurface != nullptr) {
        wl_surface_attach(_cursorSurface, nullptr, 0, 0);
        wl_surface_commit(_cursorSurface);
    }

    if (_pointer != nullptr) {
        uint32_t version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(_pointer));
        if (version >= WL_POINTER_RELEASE_SINCE_VERSION) {
            wl_pointer_release(_pointer);
        } else {
            wl_pointer_destroy(_pointer);
        }
        _pointer = nullptr;
    }
}

void jwm::WindowManagerWayland::_destroyRelativePointer() {
    if (_relativePointer == nullptr) {
        return;
    }
    zwp_relative_pointer_v1_destroy(_relativePointer);
    _relativePointer = nullptr;
}

void jwm::WindowManagerWayland::_destroyLockedPointer() {
    if (_lockedPointer == nullptr) {
        return;
    }
    zwp_locked_pointer_v1_destroy(_lockedPointer);
    _lockedPointer = nullptr;
}

void jwm::WindowManagerWayland::_updatePointerLock() {
    WindowWayland* focusWindow = _pointerFocusWindow;
    bool hasRequest = false;
    if (focusWindow != nullptr) {
        auto requestIt = _pointerLockRequests.find(focusWindow);
        if (requestIt != _pointerLockRequests.end()) {
            hasRequest = requestIt->second;
        }
    }

    bool isEligible = focusWindow != nullptr &&
        hasRequest &&
        focusWindow == _keyboardFocusWindow &&
        _pointer != nullptr &&
        _pointerConstraints != nullptr &&
        _relativePointerManager != nullptr &&
        focusWindow->getSurface() != nullptr;

    if (!isEligible) {
        _destroyLockedPointer();
        _destroyRelativePointer();
        _pointerLockWindow = nullptr;
        _isPointerLockActive = false;
        _applyCursorForFocus();
        return;
    }

    if (_relativePointer == nullptr) {
        _relativePointer = zwp_relative_pointer_manager_v1_get_relative_pointer(_relativePointerManager, _pointer);
        if (_relativePointer == nullptr) {
            JWM_LOG("Wayland: zwp_relative_pointer_manager_v1_get_relative_pointer failed");
            _applyCursorForFocus();
            return;
        }
        if (zwp_relative_pointer_v1_add_listener(_relativePointer, &kRelativePointerListener, this) != 0) {
            JWM_LOG("Wayland: zwp_relative_pointer_v1_add_listener failed");
            _destroyRelativePointer();
            _applyCursorForFocus();
            return;
        }
    }

    if (_lockedPointer == nullptr) {
        _lockedPointer = zwp_pointer_constraints_v1_lock_pointer(
            _pointerConstraints,
            focusWindow->getSurface(),
            _pointer,
            nullptr,
            ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT
        );
        if (_lockedPointer == nullptr) {
            JWM_LOG("Wayland: zwp_pointer_constraints_v1_lock_pointer failed");
            _destroyRelativePointer();
            _applyCursorForFocus();
            return;
        }
        if (zwp_locked_pointer_v1_add_listener(_lockedPointer, &kLockedPointerListener, this) != 0) {
            JWM_LOG("Wayland: zwp_locked_pointer_v1_add_listener failed");
            _destroyLockedPointer();
            _destroyRelativePointer();
            _applyCursorForFocus();
            return;
        }
    }

    _pointerLockWindow = focusWindow;
    _applyCursorForFocus();
}

void jwm::WindowManagerWayland::_resetKeyboard() {
    _handleKeyboardFocusLeave(false);
    _keyboardModifiers = 0;

    if (_xkbState != nullptr) {
        xkb_state_unref(_xkbState);
        _xkbState = nullptr;
    }
    if (_xkbKeymap != nullptr) {
        xkb_keymap_unref(_xkbKeymap);
        _xkbKeymap = nullptr;
    }

    _xkbShiftMod = XKB_MOD_INVALID;
    _xkbControlMod = XKB_MOD_INVALID;
    _xkbAltMod = XKB_MOD_INVALID;
    _xkbLogoMod = XKB_MOD_INVALID;
    _xkbCapsMod = XKB_MOD_INVALID;

    if (_keyboard != nullptr) {
        uint32_t version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(_keyboard));
        if (version >= WL_KEYBOARD_RELEASE_SINCE_VERSION) {
            wl_keyboard_release(_keyboard);
        } else {
            wl_keyboard_destroy(_keyboard);
        }
        _keyboard = nullptr;
    }
}

void jwm::WindowManagerWayland::_resetSeat() {
    _resetPointer();
    _resetKeyboard();

    if (_seat != nullptr) {
        if (_seatVersion >= WL_SEAT_RELEASE_SINCE_VERSION) {
            wl_seat_release(_seat);
        } else {
            wl_seat_destroy(_seat);
        }
        _seat = nullptr;
    }
    _seatName = std::numeric_limits<uint32_t>::max();
    _seatVersion = 0;
    _lastInputSerial = 0;
}

void jwm::WindowManagerWayland::_cleanup() {
    _runLoop = false;

    _resetSeat();
    _destroyCursorResources();
    _pointerLockRequests.clear();
    _cancelActivationRequest();

    if (_relativePointerManager != nullptr) {
        zwp_relative_pointer_manager_v1_destroy(_relativePointerManager);
        _relativePointerManager = nullptr;
    }
    _relativePointerManagerName = std::numeric_limits<uint32_t>::max();
    if (_pointerConstraints != nullptr) {
        zwp_pointer_constraints_v1_destroy(_pointerConstraints);
        _pointerConstraints = nullptr;
    }
    _pointerConstraintsName = std::numeric_limits<uint32_t>::max();

    if (_activationManager != nullptr) {
        xdg_activation_v1_destroy(_activationManager);
        _activationManager = nullptr;
    }
    _activationManagerName = std::numeric_limits<uint32_t>::max();

    if (_fractionalScaleManager != nullptr) {
        wp_fractional_scale_manager_v1_destroy(_fractionalScaleManager);
        _fractionalScaleManager = nullptr;
    }
    _fractionalScaleManagerName = std::numeric_limits<uint32_t>::max();

    if (_viewporter != nullptr) {
        wp_viewporter_destroy(_viewporter);
        _viewporter = nullptr;
    }
    _viewporterName = std::numeric_limits<uint32_t>::max();

    if (_decorationManager != nullptr) {
        zxdg_decoration_manager_v1_destroy(_decorationManager);
        _decorationManager = nullptr;
    }
    _decorationManagerName = std::numeric_limits<uint32_t>::max();

    _clearXdgOutputBindings();

    for (auto& outputPair : _outputByName) {
        if (outputPair.second->output != nullptr) {
            wl_output_destroy(outputPair.second->output);
        }
    }
    _outputByName.clear();
    _surfaceToWindow.clear();

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

    if (_xkbContext != nullptr) {
        xkb_context_unref(_xkbContext);
        _xkbContext = nullptr;
    }

    _pendingCursorWindow = nullptr;
    _lastInputSerial = 0;

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

        ScreenInfoWayland screenInfo = {
            static_cast<long>(output.name),
            IRect::makeXYWH(boundsX, boundsY, boundsWidth, boundsHeight),
            false,
            static_cast<float>(std::max(1, output.scale))
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

bool jwm::WindowManagerWayland::_bindSeat(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_seat != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 7u);
    _seat = static_cast<wl_seat*>(wl_registry_bind(registry, name, &wl_seat_interface, bindVersion));
    if (_seat == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wl_seat) failed");
        return false;
    }
    _seatName = name;
    _seatVersion = bindVersion;
    if (wl_seat_add_listener(_seat, &kSeatListener, this) != 0) {
        JWM_LOG("Wayland: wl_seat_add_listener failed");
        _resetSeat();
        return false;
    }
    return true;
}

bool jwm::WindowManagerWayland::_bindPointerConstraints(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_pointerConstraints != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _pointerConstraints = static_cast<zwp_pointer_constraints_v1*>(
        wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, bindVersion));
    if (_pointerConstraints == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(zwp_pointer_constraints_v1) failed");
        return false;
    }
    _pointerConstraintsName = name;
    _updatePointerLock();
    return true;
}

bool jwm::WindowManagerWayland::_bindRelativePointerManager(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_relativePointerManager != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _relativePointerManager = static_cast<zwp_relative_pointer_manager_v1*>(
        wl_registry_bind(registry, name, &zwp_relative_pointer_manager_v1_interface, bindVersion));
    if (_relativePointerManager == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(zwp_relative_pointer_manager_v1) failed");
        return false;
    }
    _relativePointerManagerName = name;
    _updatePointerLock();
    return true;
}

bool jwm::WindowManagerWayland::_bindDecorationManager(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_decorationManager != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _decorationManager = static_cast<zxdg_decoration_manager_v1*>(
        wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, bindVersion));
    if (_decorationManager == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(zxdg_decoration_manager_v1) failed");
        return false;
    }
    _decorationManagerName = name;
    return true;
}

bool jwm::WindowManagerWayland::_bindActivationManager(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_activationManager != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _activationManager = static_cast<xdg_activation_v1*>(
        wl_registry_bind(registry, name, &xdg_activation_v1_interface, bindVersion));
    if (_activationManager == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(xdg_activation_v1) failed");
        return false;
    }
    _activationManagerName = name;
    return true;
}

bool jwm::WindowManagerWayland::_bindViewporter(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_viewporter != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _viewporter = static_cast<wp_viewporter*>(
        wl_registry_bind(registry, name, &wp_viewporter_interface, bindVersion));
    if (_viewporter == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wp_viewporter) failed");
        return false;
    }
    _viewporterName = name;
    _notifyWindowsScaleCapabilityChanged();
    return true;
}

bool jwm::WindowManagerWayland::_bindFractionalScaleManager(wl_registry* registry, uint32_t name, uint32_t version) {
    if (_fractionalScaleManager != nullptr) {
        return true;
    }

    uint32_t bindVersion = std::min<uint32_t>(version, 1u);
    _fractionalScaleManager = static_cast<wp_fractional_scale_manager_v1*>(
        wl_registry_bind(registry, name, &wp_fractional_scale_manager_v1_interface, bindVersion));
    if (_fractionalScaleManager == nullptr) {
        JWM_LOG("Wayland: wl_registry_bind(wp_fractional_scale_manager_v1) failed");
        return false;
    }
    _fractionalScaleManagerName = name;
    _notifyWindowsScaleCapabilityChanged();
    return true;
}

void jwm::WindowManagerWayland::_notifyWindowsOutputMetricsChanged(wl_output* output) {
    std::vector<WindowWayland*> windows;
    windows.reserve(_surfaceToWindow.size());
    for (const auto& surfaceEntry : _surfaceToWindow) {
        windows.push_back(surfaceEntry.second);
    }
    for (WindowWayland* window : windows) {
        if (window != nullptr) {
            window->handleOutputMetricsChanged(output);
        }
    }
}

void jwm::WindowManagerWayland::_notifyWindowsScaleCapabilityChanged() {
    std::vector<WindowWayland*> windows;
    windows.reserve(_surfaceToWindow.size());
    for (const auto& surfaceEntry : _surfaceToWindow) {
        windows.push_back(surfaceEntry.second);
    }
    for (WindowWayland* window : windows) {
        if (window != nullptr) {
            window->handleScaleProtocolGlobalsChanged();
        }
    }
}

void jwm::WindowManagerWayland::_cancelActivationRequest() {
    if (_activationToken != nullptr) {
        xdg_activation_token_v1_destroy(_activationToken);
        _activationToken = nullptr;
    }
    _activationSurface = nullptr;
    _activationWindow = nullptr;
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

zxdg_decoration_manager_v1* jwm::WindowManagerWayland::getDecorationManager() const {
    return _decorationManager;
}

wp_viewporter* jwm::WindowManagerWayland::getViewporter() const {
    return _viewporter;
}

wp_fractional_scale_manager_v1* jwm::WindowManagerWayland::getFractionalScaleManager() const {
    return _fractionalScaleManager;
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

void jwm::WindowManagerWayland::registerWindowSurface(wl_surface* surface, WindowWayland* window) {
    if (surface == nullptr || window == nullptr) {
        return;
    }
    _surfaceToWindow[surface] = window;
}

void jwm::WindowManagerWayland::unregisterWindowSurface(wl_surface* surface) {
    if (surface == nullptr) {
        return;
    }
    auto it = _surfaceToWindow.find(surface);
    if (it == _surfaceToWindow.end()) {
        return;
    }

    WindowWayland* window = it->second;
    _surfaceToWindow.erase(it);
    _pointerLockRequests.erase(window);
    if (_activationWindow == window || _activationSurface == surface) {
        _cancelActivationRequest();
    }

    if (_pointerFocusWindow == window) {
        _pointerFocusWindow = nullptr;
        _pointerEnterSerial = 0;
        _pointerButtonMask = 0;
        _pointerAxisPending = false;
        _pointerAxisDiscretePending = false;
        _pointerAxisValue120Pending = false;
        _pointerAxisX = 0.0;
        _pointerAxisY = 0.0;
        _pointerAxisDiscreteX = 0.0;
        _pointerAxisDiscreteY = 0.0;
        _pointerAxisValue120X = 0.0;
        _pointerAxisValue120Y = 0.0;
    }
    if (_keyboardFocusWindow == window && !_isHandlingKeyboardFocusLeave) {
        _handleKeyboardFocusLeave(false);
    }
    if (_pendingCursorWindow == window) {
        _pendingCursorWindow = nullptr;
    }

    _updatePointerLock();
}

void jwm::WindowManagerWayland::requestCursorUpdate(WindowWayland* window) {
    _pendingCursorWindow = window;
    _applyCursorForFocus();
}

void jwm::WindowManagerWayland::requestPointerLock(WindowWayland* window, bool isLocked) {
    if (window == nullptr) {
        return;
    }

    if (isLocked) {
        _pointerLockRequests[window] = true;
    } else {
        _pointerLockRequests.erase(window);
    }

    _updatePointerLock();
}

bool jwm::WindowManagerWayland::requestActivation(WindowWayland* window, wl_surface* surface) {
    if (_activationManager == nullptr || window == nullptr || surface == nullptr) {
        return false;
    }
    if (_windowBySurface(surface) != window) {
        return false;
    }

    _cancelActivationRequest();

    _activationToken = xdg_activation_v1_get_activation_token(_activationManager);
    if (_activationToken == nullptr) {
        JWM_LOG("Wayland: xdg_activation_v1_get_activation_token failed");
        return false;
    }

    _activationSurface = surface;
    _activationWindow = window;
    if (xdg_activation_token_v1_add_listener(_activationToken, &kActivationTokenListener, this) != 0) {
        JWM_LOG("Wayland: xdg_activation_token_v1_add_listener failed");
        _cancelActivationRequest();
        return false;
    }

    xdg_activation_token_v1_set_surface(_activationToken, surface);
    if (_seat != nullptr && _lastInputSerial != 0) {
        xdg_activation_token_v1_set_serial(_activationToken, _lastInputSerial, _seat);
    }
    xdg_activation_token_v1_commit(_activationToken);
    return true;
}

bool jwm::WindowManagerWayland::isKeyboardFocusedWindow(const WindowWayland* window) const {
    return window != nullptr && _keyboardFocusWindow == window;
}

bool jwm::WindowManagerWayland::tryGetScreenForOutput(wl_output* output, ScreenInfoWayland& screen) const {
    if (output == nullptr) {
        return false;
    }

    for (const auto& entry : _outputByName) {
        const WaylandOutputState& outputState = *entry.second;
        if (outputState.output != output || !outputState.hasMode || outputState.width <= 0 || outputState.height <= 0) {
            continue;
        }

        int32_t boundsX = outputState.hasLogicalPosition ? outputState.logicalX : outputState.x;
        int32_t boundsY = outputState.hasLogicalPosition ? outputState.logicalY : outputState.y;
        int32_t boundsWidth = outputState.hasLogicalSize ? outputState.logicalWidth : outputState.width;
        int32_t boundsHeight = outputState.hasLogicalSize ? outputState.logicalHeight : outputState.height;
        if (boundsWidth <= 0 || boundsHeight <= 0) {
            return false;
        }

        screen = ScreenInfoWayland {
            static_cast<long>(outputState.name),
            IRect::makeXYWH(boundsX, boundsY, boundsWidth, boundsHeight),
            false,
            static_cast<float>(std::max(1, outputState.scale))
        };
        return true;
    }

    return false;
}

int jwm::WindowManagerWayland::getOutputScale(wl_output* output) const {
    if (output == nullptr) {
        return 1;
    }

    for (const auto& entry : _outputByName) {
        if (entry.second->output == output) {
            return std::max(1, entry.second->scale);
        }
    }
    return 1;
}

bool jwm::WindowManagerWayland::hasOutput(wl_output* output) const {
    if (output == nullptr) {
        return false;
    }

    for (const auto& entry : _outputByName) {
        if (entry.second->output == output) {
            return true;
        }
    }
    return false;
}

jwm::WindowWayland* jwm::WindowManagerWayland::_windowBySurface(wl_surface* surface) const {
    auto it = _surfaceToWindow.find(surface);
    if (it == _surfaceToWindow.end()) {
        return nullptr;
    }
    return it->second;
}

bool jwm::WindowManagerWayland::_ensureCursorResources() {
    if (_cursorTheme != nullptr && _cursorSurface != nullptr) {
        return true;
    }
    if (_shm == nullptr || _compositor == nullptr) {
        return false;
    }

    if (_cursorTheme == nullptr) {
        int themeSize = _cursorThemeSize;
        const char* envSize = std::getenv("XCURSOR_SIZE");
        if (envSize != nullptr) {
            int parsed = std::atoi(envSize);
            if (parsed > 0) {
                themeSize = parsed;
            }
        }
        _cursorTheme = wl_cursor_theme_load(nullptr, themeSize, _shm);
        if (_cursorTheme == nullptr) {
            JWM_LOG("Wayland: wl_cursor_theme_load failed");
            return false;
        }
        _cursorThemeSize = themeSize;
    }

    if (_cursorSurface == nullptr) {
        _cursorSurface = wl_compositor_create_surface(_compositor);
        if (_cursorSurface == nullptr) {
            JWM_LOG("Wayland: failed to create cursor surface");
            return false;
        }
    }

    return true;
}

wl_cursor* jwm::WindowManagerWayland::_cursorForType(MouseCursor cursorType) {
    auto cached = _cursorByType.find(cursorType);
    if (cached != _cursorByType.end()) {
        return cached->second;
    }

    wl_cursor* cursor = nullptr;
    auto candidates = cursorNamesForType(cursorType);
    for (const char* name : candidates) {
        cursor = wl_cursor_theme_get_cursor(_cursorTheme, name);
        if (cursor != nullptr) {
            break;
        }
    }

    if (cursor == nullptr) {
        cursor = wl_cursor_theme_get_cursor(_cursorTheme, "default");
    }

    _cursorByType[cursorType] = cursor;
    return cursor;
}

void jwm::WindowManagerWayland::_destroyCursorResources() {
    _cursorByType.clear();

    if (_cursorSurface != nullptr) {
        wl_surface_destroy(_cursorSurface);
        _cursorSurface = nullptr;
    }
    if (_cursorTheme != nullptr) {
        wl_cursor_theme_destroy(_cursorTheme);
        _cursorTheme = nullptr;
    }
}

void jwm::WindowManagerWayland::_applyCursorForFocus() {
    if (_pointer == nullptr || _pointerFocusWindow == nullptr || _pointerEnterSerial == 0) {
        return;
    }

    if (_isPointerLockActive && _pointerLockWindow == _pointerFocusWindow) {
        wl_pointer_set_cursor(_pointer, _pointerEnterSerial, nullptr, 0, 0);
        _pendingCursorWindow = nullptr;
        return;
    }

    if (!_ensureCursorResources()) {
        return;
    }

    wl_cursor* cursor = _cursorForType(_pointerFocusWindow->_mouseCursor);
    if (cursor == nullptr || cursor->image_count == 0 || cursor->images == nullptr || cursor->images[0] == nullptr) {
        wl_pointer_set_cursor(_pointer, _pointerEnterSerial, nullptr, 0, 0);
        _pendingCursorWindow = nullptr;
        return;
    }

    wl_cursor_image* image = cursor->images[0];
    wl_buffer* buffer = wl_cursor_image_get_buffer(image);
    if (buffer == nullptr) {
        wl_pointer_set_cursor(_pointer, _pointerEnterSerial, nullptr, 0, 0);
        _pendingCursorWindow = nullptr;
        return;
    }

    wl_pointer_set_cursor(
        _pointer,
        _pointerEnterSerial,
        _cursorSurface,
        static_cast<int32_t>(image->hotspot_x),
        static_cast<int32_t>(image->hotspot_y)
    );
    wl_surface_attach(_cursorSurface, buffer, 0, 0);
    wl_surface_damage(_cursorSurface, 0, 0, static_cast<int32_t>(image->width), static_cast<int32_t>(image->height));
    wl_surface_commit(_cursorSurface);
    _pendingCursorWindow = nullptr;
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

int jwm::WindowManagerWayland::_getPollTimeoutMillis() const {
    if (!_repeat.isActive) {
        return -1;
    }

    auto now = std::chrono::steady_clock::now();
    if (_repeat.nextAt <= now) {
        return 0;
    }

    auto timeout = std::chrono::duration_cast<std::chrono::milliseconds>(_repeat.nextAt - now).count();
    if (timeout > std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(timeout);
}

void jwm::WindowManagerWayland::_dispatchRepeatIfNeeded() {
    if (!_repeat.isActive || _keyboardFocusWindow == nullptr) {
        return;
    }

    auto now = std::chrono::steady_clock::now();
    if (_repeat.nextAt > now) {
        return;
    }

    _dispatchKey(
        _keyboardFocusWindow,
        _repeat.keycode,
        true,
        _repeat.key,
        _repeat.location,
        _repeat.extraModifiers,
        true
    );

    if (_repeatRate <= 0) {
        _repeat.isActive = false;
        return;
    }

    int intervalMs = std::max(1, 1000 / _repeatRate);
    _repeat.nextAt = now + std::chrono::milliseconds(intervalMs);
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
        _dispatchRepeatIfNeeded();

        while (wl_display_prepare_read(_display) != 0) {
            if (wl_display_dispatch_pending(_display) < 0) {
                _runLoop = false;
                break;
            }
            _processTasks();
            _dispatchRepeatIfNeeded();
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

        int pollStatus = poll(events, 2, _getPollTimeoutMillis());
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

void jwm::WindowManagerWayland::_refreshKeyboardModifiers() {
    int modifiers = KeyWayland::getModifiers();

    if (_xkbState != nullptr) {
        auto isActive = [this](uint32_t modIdx) {
            if (modIdx == XKB_MOD_INVALID) {
                return false;
            }
            return xkb_state_mod_index_is_active(_xkbState, modIdx, XKB_STATE_MODS_EFFECTIVE) > 0;
        };

        if (isActive(_xkbShiftMod)) modifiers |= (int) KeyModifier::SHIFT;
        if (isActive(_xkbControlMod)) modifiers |= (int) KeyModifier::CONTROL;
        if (isActive(_xkbAltMod)) modifiers |= (int) KeyModifier::ALT;
        if (isActive(_xkbLogoMod)) modifiers |= (int) KeyModifier::LINUX_SUPER;
        if (isActive(_xkbCapsMod)) modifiers |= (int) KeyModifier::CAPS_LOCK;
    }

    _keyboardModifiers = modifiers;
}

bool jwm::WindowManagerWayland::_translateKeycode(uint32_t keycode, Key& key, KeyLocation& location, int& extraModifiers) const {
    key = Key::UNDEFINED;
    location = KeyLocation::DEFAULT;
    extraModifiers = 0;

    if (_xkbState == nullptr) {
        return false;
    }

    xkb_keycode_t xkbCode = static_cast<xkb_keycode_t>(keycode + kXkbKeycodeOffset);
    xkb_keysym_t keysym = xkb_state_key_get_one_sym(_xkbState, xkbCode);
    key = KeyWayland::fromKeysym(keysym, location, extraModifiers);
    return true;
}

bool jwm::WindowManagerWayland::_shouldRepeatKey(uint32_t keycode) const {
    if (_xkbKeymap == nullptr || _repeatRate <= 0) {
        return false;
    }
    xkb_keycode_t xkbCode = static_cast<xkb_keycode_t>(keycode + kXkbKeycodeOffset);
    return xkb_keymap_key_repeats(_xkbKeymap, xkbCode) > 0;
}

void jwm::WindowManagerWayland::_scheduleRepeat(uint32_t keycode, Key key, KeyLocation location, int extraModifiers) {
    if (_repeatRate <= 0 || _repeatDelay < 0) {
        return;
    }

    _repeat.isActive = true;
    _repeat.keycode = keycode;
    _repeat.key = key;
    _repeat.location = location;
    _repeat.extraModifiers = extraModifiers;
    _repeat.nextAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(_repeatDelay);
}

void jwm::WindowManagerWayland::_cancelRepeat(uint32_t keycode) {
    if (!_repeat.isActive) {
        return;
    }
    if (keycode != std::numeric_limits<uint32_t>::max() && keycode != _repeat.keycode) {
        return;
    }
    _repeat.isActive = false;
}

void jwm::WindowManagerWayland::_dispatchTextInput(WindowWayland* window, uint32_t keycode) {
    if (window == nullptr || _xkbState == nullptr) {
        return;
    }

    xkb_keycode_t xkbCode = static_cast<xkb_keycode_t>(keycode + kXkbKeycodeOffset);
    char utf8[128];
    int count = xkb_state_key_get_utf8(_xkbState, xkbCode, utf8, sizeof(utf8));
    if (count <= 0) {
        return;
    }

    if (count >= static_cast<int>(sizeof(utf8))) {
        count = static_cast<int>(sizeof(utf8)) - 1;
    }
    utf8[count] = 0;

    unsigned char first = static_cast<unsigned char>(utf8[0]);
    if (first == 127 || first <= 0x1f) {
        return;
    }

    JNIEnv* env = getWaylandJniEnv();
    if (env == nullptr) {
        return;
    }

    StringUTF16 text = utf8;
    JNILocal<jstring> jText = text.toJString(env);
    JNILocal<jobject> eventText(env, classes::EventTextInput::make(env, jText.get()));
    window->dispatch(eventText.get());
}

void jwm::WindowManagerWayland::_dispatchKey(WindowWayland* window, uint32_t keycode, bool isPressed, Key key, KeyLocation location, int extraModifiers, bool emitTextInput) {
    if (window == nullptr) {
        return;
    }

    if (key != Key::UNDEFINED) {
        KeyWayland::setKeyState(key, isPressed);
    }

    _refreshKeyboardModifiers();

    JNIEnv* env = getWaylandJniEnv();
    if (env == nullptr) {
        return;
    }

    int modifiers = _keyboardModifiers | extraModifiers;
    JNILocal<jobject> eventKey(env, classes::EventKey::make(env, key, static_cast<jboolean>(isPressed), modifiers, location));
    window->dispatch(eventKey.get());

    if (emitTextInput && isPressed) {
        _dispatchTextInput(window, keycode);
    }
}

void jwm::WindowManagerWayland::_handleKeyboardFocusEnter(WindowWayland* window, wl_array* keys) {
    bool focusChanged = window != _keyboardFocusWindow;
    if (_keyboardFocusWindow != nullptr && _keyboardFocusWindow != window) {
        _handleKeyboardFocusLeave(true);
    }

    _keyboardFocusWindow = window;
    _pressedKeys.clear();
    KeyWayland::clearKeyStates();

    if (window != nullptr && keys != nullptr && _xkbState != nullptr) {
        uint32_t* pressed = static_cast<uint32_t*>(keys->data);
        size_t count = keys->size / sizeof(uint32_t);
        for (size_t i = 0; i < count; ++i) {
            Key key;
            KeyLocation location;
            int extraModifiers;
            if (!_translateKeycode(pressed[i], key, location, extraModifiers) || key == Key::UNDEFINED) {
                continue;
            }

            KeyWayland::setKeyState(key, true);
            _pressedKeys[pressed[i]] = PressedKeyState {
                key,
                location,
                extraModifiers,
                _shouldRepeatKey(pressed[i])
            };
        }
    }

    _refreshKeyboardModifiers();

    if (focusChanged && window != nullptr) {
        window->dispatch(classes::EventWindowFocusIn::kInstance);
    }
    _updatePointerLock();
}

void jwm::WindowManagerWayland::_handleKeyboardFocusLeave(bool dispatchFocusOut) {
    if (_isHandlingKeyboardFocusLeave) {
        return;
    }
    _isHandlingKeyboardFocusLeave = true;

    WindowWayland* focusedWindow = _keyboardFocusWindow;
    std::vector<std::pair<uint32_t, PressedKeyState>> pressedKeysSnapshot;
    pressedKeysSnapshot.reserve(_pressedKeys.size());
    for (const auto& entry : _pressedKeys) {
        pressedKeysSnapshot.push_back(entry);
    }

    // Clear manager focus/key state first so user callbacks cannot re-enter
    // and reprocess the same key set while we dispatch synthetic releases.
    _keyboardFocusWindow = nullptr;
    _pressedKeys.clear();
    _cancelRepeat(std::numeric_limits<uint32_t>::max());

    for (const auto& entry : pressedKeysSnapshot) {
        const PressedKeyState& state = entry.second;
        _dispatchKey(focusedWindow, entry.first, false, state.key, state.location, state.extraModifiers, false);
    }

    KeyWayland::clearKeyStates();
    _refreshKeyboardModifiers();

    if (dispatchFocusOut && focusedWindow != nullptr) {
        focusedWindow->dispatch(classes::EventWindowFocusOut::kInstance);
    }
    _updatePointerLock();
    _isHandlingKeyboardFocusLeave = false;
}

void jwm::WindowManagerWayland::_dispatchMouseMove(WindowWayland* window, int movementX, int movementY) {
    if (window == nullptr) {
        return;
    }

    JNIEnv* env = getWaylandJniEnv();
    if (env == nullptr) {
        return;
    }

    JNILocal<jobject> eventMove(env, classes::EventMouseMove::make(
        env,
        _pointerContentX,
        _pointerContentY,
        movementX,
        movementY,
        _pointerButtonMask,
        _keyboardModifiers
    ));
    window->dispatch(eventMove.get());
}

void jwm::WindowManagerWayland::_dispatchMouseButton(WindowWayland* window, MouseButton button, bool isPressed) {
    if (window == nullptr) {
        return;
    }

    JNIEnv* env = getWaylandJniEnv();
    if (env == nullptr) {
        return;
    }

    JNILocal<jobject> eventButton(env, classes::EventMouseButton::make(
        env,
        button,
        static_cast<jboolean>(isPressed),
        _pointerContentX,
        _pointerContentY,
        _keyboardModifiers
    ));
    window->dispatch(eventButton.get());
}

void jwm::WindowManagerWayland::_dispatchMouseScroll(WindowWayland* window, float deltaX, float deltaY, float deltaChars, float deltaLines) {
    if (window == nullptr) {
        return;
    }

    JNIEnv* env = getWaylandJniEnv();
    if (env == nullptr) {
        return;
    }

    JNILocal<jobject> eventScroll(env, classes::EventMouseScroll::make(
        env,
        deltaX,
        deltaY,
        deltaChars,
        deltaLines,
        0.0f,
        _pointerContentX,
        _pointerContentY,
        _keyboardModifiers
    ));
    window->dispatch(eventScroll.get());
}

void jwm::WindowManagerWayland::_flushPointerAxis() {
    if (!_pointerAxisPending && !_pointerAxisDiscretePending && !_pointerAxisValue120Pending) {
        return;
    }

    WindowWayland* window = _pointerFocusWindow;
    if (window != nullptr) {
        float deltaX = static_cast<float>(-_pointerAxisX * kPixelsPerScroll);
        float deltaY = static_cast<float>(-_pointerAxisY * kPixelsPerScroll);
        float deltaChars = static_cast<float>(-_pointerAxisDiscreteX);
        float deltaLines = static_cast<float>(-_pointerAxisDiscreteY);

        if (_pointerAxisValue120X != 0.0) {
            deltaChars = static_cast<float>(-_pointerAxisValue120X / 120.0);
        }
        if (_pointerAxisValue120Y != 0.0) {
            deltaLines = static_cast<float>(-_pointerAxisValue120Y / 120.0);
        }

        if (deltaX == 0.0f && deltaChars != 0.0f) {
            deltaX = deltaChars * kPixelsPerScroll;
        }
        if (deltaY == 0.0f && deltaLines != 0.0f) {
            deltaY = deltaLines * kPixelsPerScroll;
        }

        _dispatchMouseScroll(window, deltaX, deltaY, deltaChars, deltaLines);
    }

    _pointerAxisPending = false;
    _pointerAxisDiscretePending = false;
    _pointerAxisValue120Pending = false;
    _pointerAxisX = 0.0;
    _pointerAxisY = 0.0;
    _pointerAxisDiscreteX = 0.0;
    _pointerAxisDiscreteY = 0.0;
    _pointerAxisValue120X = 0.0;
    _pointerAxisValue120Y = 0.0;
}

void jwm::WindowManagerWayland::onActivationTokenDone(void* data, xdg_activation_token_v1* token, const char* tokenString) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (manager->_activationToken != token) {
        if (token != nullptr) {
            xdg_activation_token_v1_destroy(token);
        }
        return;
    }

    bool canActivate =
        manager->_activationManager != nullptr &&
        manager->_activationSurface != nullptr &&
        manager->_activationWindow != nullptr &&
        tokenString != nullptr &&
        tokenString[0] != '\0' &&
        manager->_windowBySurface(manager->_activationSurface) == manager->_activationWindow;

    if (canActivate) {
        xdg_activation_v1_activate(manager->_activationManager, tokenString, manager->_activationSurface);
    }

    manager->_cancelActivationRequest();
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
    if (strcmp(interface, wl_seat_interface.name) == 0) {
        manager->_bindSeat(registry, name, version);
        return;
    }
    if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        manager->_bindPointerConstraints(registry, name, version);
        return;
    }
    if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        manager->_bindRelativePointerManager(registry, name, version);
        return;
    }
    if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        manager->_bindDecorationManager(registry, name, version);
        return;
    }
    if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        manager->_bindActivationManager(registry, name, version);
        return;
    }
    if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        manager->_bindViewporter(registry, name, version);
        return;
    }
    if (strcmp(interface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        manager->_bindFractionalScaleManager(registry, name, version);
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
        manager->_destroyCursorResources();
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
        manager->_destroyCursorResources();
        if (manager->_shm != nullptr) {
            wl_shm_destroy(manager->_shm);
            manager->_shm = nullptr;
        }
        manager->_shmName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_seatName) {
        manager->_resetSeat();
        return;
    }
    if (name == manager->_pointerConstraintsName) {
        manager->_destroyLockedPointer();
        manager->_destroyRelativePointer();
        manager->_pointerLockWindow = nullptr;
        manager->_isPointerLockActive = false;
        if (manager->_pointerConstraints != nullptr) {
            zwp_pointer_constraints_v1_destroy(manager->_pointerConstraints);
            manager->_pointerConstraints = nullptr;
        }
        manager->_pointerConstraintsName = std::numeric_limits<uint32_t>::max();
        manager->_applyCursorForFocus();
        return;
    }
    if (name == manager->_relativePointerManagerName) {
        manager->_destroyLockedPointer();
        manager->_destroyRelativePointer();
        manager->_pointerLockWindow = nullptr;
        manager->_isPointerLockActive = false;
        if (manager->_relativePointerManager != nullptr) {
            zwp_relative_pointer_manager_v1_destroy(manager->_relativePointerManager);
            manager->_relativePointerManager = nullptr;
        }
        manager->_relativePointerManagerName = std::numeric_limits<uint32_t>::max();
        manager->_applyCursorForFocus();
        return;
    }
    if (name == manager->_decorationManagerName) {
        if (manager->_decorationManager != nullptr) {
            zxdg_decoration_manager_v1_destroy(manager->_decorationManager);
            manager->_decorationManager = nullptr;
        }
        manager->_decorationManagerName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_activationManagerName) {
        manager->_cancelActivationRequest();
        if (manager->_activationManager != nullptr) {
            xdg_activation_v1_destroy(manager->_activationManager);
            manager->_activationManager = nullptr;
        }
        manager->_activationManagerName = std::numeric_limits<uint32_t>::max();
        return;
    }
    if (name == manager->_viewporterName) {
        if (manager->_viewporter != nullptr) {
            wp_viewporter_destroy(manager->_viewporter);
            manager->_viewporter = nullptr;
        }
        manager->_viewporterName = std::numeric_limits<uint32_t>::max();
        manager->_notifyWindowsScaleCapabilityChanged();
        return;
    }
    if (name == manager->_fractionalScaleManagerName) {
        if (manager->_fractionalScaleManager != nullptr) {
            wp_fractional_scale_manager_v1_destroy(manager->_fractionalScaleManager);
            manager->_fractionalScaleManager = nullptr;
        }
        manager->_fractionalScaleManagerName = std::numeric_limits<uint32_t>::max();
        manager->_notifyWindowsScaleCapabilityChanged();
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
        manager->_notifyWindowsOutputMetricsChanged(nullptr);
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
    wl_output* removedOutput = outputIt->second->output;
    wl_output_destroy(outputIt->second->output);
    manager->_outputByName.erase(outputIt);
    manager->_rebuildScreens();
    manager->_notifyWindowsOutputMetricsChanged(removedOutput);
}

void jwm::WindowManagerWayland::onOutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->x = x;
    outputState->y = y;
    (void) transform;
    outputState->manager->_rebuildScreens();
    outputState->manager->_notifyWindowsOutputMetricsChanged(output);
}

void jwm::WindowManagerWayland::onOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    if ((flags & WL_OUTPUT_MODE_CURRENT) != 0 || !outputState->hasMode) {
        outputState->width = width;
        outputState->height = height;
        outputState->hasMode = true;
    }
    outputState->manager->_rebuildScreens();
    outputState->manager->_notifyWindowsOutputMetricsChanged(output);
}

void jwm::WindowManagerWayland::onOutputDone(void* data, wl_output* output) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->manager->_rebuildScreens();
}

void jwm::WindowManagerWayland::onOutputScale(void* data, wl_output* output, int32_t factor) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->scale = std::max(1, factor);
    outputState->manager->_rebuildScreens();
    outputState->manager->_notifyWindowsOutputMetricsChanged(output);
}

void jwm::WindowManagerWayland::onXdgOutputLogicalPosition(void* data, zxdg_output_v1* xdgOutput, int32_t x, int32_t y) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->logicalX = x;
    outputState->logicalY = y;
    outputState->hasLogicalPosition = true;
    outputState->manager->_rebuildScreens();
    outputState->manager->_notifyWindowsOutputMetricsChanged(outputState->output);
}

void jwm::WindowManagerWayland::onXdgOutputLogicalSize(void* data, zxdg_output_v1* xdgOutput, int32_t width, int32_t height) {
    WaylandOutputState* outputState = static_cast<WaylandOutputState*>(data);
    outputState->logicalWidth = width;
    outputState->logicalHeight = height;
    outputState->hasLogicalSize = true;
    outputState->manager->_rebuildScreens();
    outputState->manager->_notifyWindowsOutputMetricsChanged(outputState->output);
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

void jwm::WindowManagerWayland::onSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);

    bool hasPointer = (capabilities & WL_SEAT_CAPABILITY_POINTER) != 0;
    if (hasPointer && manager->_pointer == nullptr) {
        manager->_pointer = wl_seat_get_pointer(seat);
        if (manager->_pointer != nullptr) {
            if (wl_pointer_add_listener(manager->_pointer, &kPointerListener, manager) != 0) {
                JWM_LOG("Wayland: wl_pointer_add_listener failed");
                manager->_resetPointer();
            }
        }
    } else if (!hasPointer && manager->_pointer != nullptr) {
        manager->_resetPointer();
    }
    manager->_updatePointerLock();

    bool hasKeyboard = (capabilities & WL_SEAT_CAPABILITY_KEYBOARD) != 0;
    if (hasKeyboard && manager->_keyboard == nullptr) {
        manager->_keyboard = wl_seat_get_keyboard(seat);
        if (manager->_keyboard != nullptr) {
            if (wl_keyboard_add_listener(manager->_keyboard, &kKeyboardListener, manager) != 0) {
                JWM_LOG("Wayland: wl_keyboard_add_listener failed");
                manager->_resetKeyboard();
            }
        }
    } else if (!hasKeyboard && manager->_keyboard != nullptr) {
        manager->_resetKeyboard();
    }
    manager->_updatePointerLock();
}

void jwm::WindowManagerWayland::onSeatName(void* data, wl_seat* seat, const char* name) {
    (void) data;
    (void) seat;
    (void) name;
}

void jwm::WindowManagerWayland::onPointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, int32_t sx, int32_t sy) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);

    WindowWayland* window = manager->_windowBySurface(surface);
    manager->_pointerFocusWindow = window;
    manager->_pointerEnterSerial = serial;
    manager->_lastInputSerial = serial;

    if (window == nullptr) {
        return;
    }

    window->toContentPixels(wl_fixed_to_double(sx), wl_fixed_to_double(sy), manager->_pointerContentX, manager->_pointerContentY);

    manager->_dispatchMouseMove(window, 0, 0);
    manager->_updatePointerLock();
    manager->_applyCursorForFocus();
}

void jwm::WindowManagerWayland::onPointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_flushPointerAxis();
    manager->_destroyLockedPointer();
    manager->_destroyRelativePointer();
    manager->_pointerLockWindow = nullptr;
    manager->_isPointerLockActive = false;
    manager->_pointerFocusWindow = nullptr;
    manager->_pointerEnterSerial = 0;
    manager->_pointerButtonMask = 0;
    manager->_pointerAxisValue120Pending = false;
    manager->_pointerAxisValue120X = 0.0;
    manager->_pointerAxisValue120Y = 0.0;
    manager->_updatePointerLock();
}

void jwm::WindowManagerWayland::onPointerMotion(void* data, wl_pointer* pointer, uint32_t time, int32_t sx, int32_t sy) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (manager->_pointerFocusWindow == nullptr) {
        return;
    }
    if (manager->_isPointerLockActive && manager->_pointerLockWindow == manager->_pointerFocusWindow) {
        return;
    }

    int previousX = manager->_pointerContentX;
    int previousY = manager->_pointerContentY;
    manager->_pointerFocusWindow->toContentPixels(wl_fixed_to_double(sx), wl_fixed_to_double(sy), manager->_pointerContentX, manager->_pointerContentY);

    int movementX = manager->_pointerContentX - previousX;
    int movementY = manager->_pointerContentY - previousY;
    manager->_dispatchMouseMove(manager->_pointerFocusWindow, movementX, movementY);
}

void jwm::WindowManagerWayland::onPointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_lastInputSerial = serial;
    if (manager->_pointerFocusWindow == nullptr || !MouseButtonWayland::isButton(button)) {
        return;
    }

    bool isPressed = state == WL_POINTER_BUTTON_STATE_PRESSED;
    int buttonMask = MouseButtonWayland::maskForButton(button);
    if (isPressed) {
        manager->_pointerButtonMask |= buttonMask;
    } else {
        manager->_pointerButtonMask &= ~buttonMask;
    }

    manager->_dispatchMouseButton(manager->_pointerFocusWindow, MouseButtonWayland::fromNative(button), isPressed);
}

void jwm::WindowManagerWayland::onPointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, int32_t value) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (manager->_pointerFocusWindow == nullptr) {
        return;
    }

    double axisValue = wl_fixed_to_double(value);
    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        manager->_pointerAxisX += axisValue;
        manager->_pointerAxisPending = true;
    } else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        manager->_pointerAxisY += axisValue;
        manager->_pointerAxisPending = true;
    }

    uint32_t version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer));
    if (version < WL_POINTER_FRAME_SINCE_VERSION) {
        manager->_flushPointerAxis();
    }
}

void jwm::WindowManagerWayland::onPointerFrame(void* data, wl_pointer* pointer) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_flushPointerAxis();
}

void jwm::WindowManagerWayland::onPointerAxisSource(void* data, wl_pointer* pointer, uint32_t axisSource) {
    (void) data;
    (void) pointer;
    (void) axisSource;
}

void jwm::WindowManagerWayland::onPointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis) {
    (void) pointer;
    (void) time;
    (void) axis;
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_flushPointerAxis();
}

void jwm::WindowManagerWayland::onPointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (manager->_pointerFocusWindow == nullptr) {
        return;
    }

    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        manager->_pointerAxisDiscreteX += static_cast<double>(discrete);
        manager->_pointerAxisDiscretePending = true;
    } else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        manager->_pointerAxisDiscreteY += static_cast<double>(discrete);
        manager->_pointerAxisDiscretePending = true;
    }

    uint32_t version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer));
    if (version < WL_POINTER_FRAME_SINCE_VERSION) {
        manager->_flushPointerAxis();
    }
}

void jwm::WindowManagerWayland::onPointerAxisValue120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (manager->_pointerFocusWindow == nullptr) {
        return;
    }

    if (axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL) {
        manager->_pointerAxisValue120X += static_cast<double>(value120);
        manager->_pointerAxisValue120Pending = true;
    } else if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) {
        manager->_pointerAxisValue120Y += static_cast<double>(value120);
        manager->_pointerAxisValue120Pending = true;
    }

    uint32_t version = wl_proxy_get_version(reinterpret_cast<wl_proxy*>(pointer));
    if (version < WL_POINTER_FRAME_SINCE_VERSION) {
        manager->_flushPointerAxis();
    }
}

void jwm::WindowManagerWayland::onPointerAxisRelativeDirection(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction) {
    (void) data;
    (void) pointer;
    (void) axis;
    (void) direction;
}

void jwm::WindowManagerWayland::onRelativePointerMotion(void* data, zwp_relative_pointer_v1* relativePointer, uint32_t utimeHi, uint32_t utimeLo, int32_t dx, int32_t dy, int32_t dxUnaccel, int32_t dyUnaccel) {
    (void) relativePointer;
    (void) utimeHi;
    (void) utimeLo;
    (void) dxUnaccel;
    (void) dyUnaccel;

    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    if (!manager->_isPointerLockActive || manager->_pointerLockWindow == nullptr) {
        return;
    }

    int movementX = manager->_pointerLockWindow->toContentPixels(wl_fixed_to_double(dx));
    int movementY = manager->_pointerLockWindow->toContentPixels(wl_fixed_to_double(dy));
    if (movementX == 0 && movementY == 0) {
        return;
    }

    manager->_dispatchMouseMove(manager->_pointerLockWindow, movementX, movementY);
}

void jwm::WindowManagerWayland::onLockedPointerLocked(void* data, zwp_locked_pointer_v1* lockedPointer) {
    (void) lockedPointer;
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_isPointerLockActive = true;
    manager->_applyCursorForFocus();
}

void jwm::WindowManagerWayland::onLockedPointerUnlocked(void* data, zwp_locked_pointer_v1* lockedPointer) {
    (void) lockedPointer;
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_isPointerLockActive = false;
    manager->_destroyLockedPointer();
    manager->_destroyRelativePointer();
    manager->_pointerLockWindow = nullptr;
    manager->_applyCursorForFocus();
    manager->_updatePointerLock();
}

void jwm::WindowManagerWayland::onKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);

    if (fd < 0) {
        return;
    }

    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1 || manager->_xkbContext == nullptr) {
        close(fd);
        return;
    }

    char* keymapData = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (keymapData == MAP_FAILED) {
        close(fd);
        JWM_LOG("Wayland: mmap for keymap failed");
        return;
    }

    xkb_keymap* keymap = xkb_keymap_new_from_string(
        manager->_xkbContext,
        keymapData,
        XKB_KEYMAP_FORMAT_TEXT_V1,
        XKB_KEYMAP_COMPILE_NO_FLAGS
    );
    munmap(keymapData, size);
    close(fd);

    if (keymap == nullptr) {
        JWM_LOG("Wayland: xkb_keymap_new_from_string failed");
        return;
    }

    xkb_state* state = xkb_state_new(keymap);
    if (state == nullptr) {
        JWM_LOG("Wayland: xkb_state_new failed");
        xkb_keymap_unref(keymap);
        return;
    }

    if (!manager->_pressedKeys.empty()) {
        WindowWayland* focusedWindow = manager->_keyboardFocusWindow;
        std::vector<std::pair<uint32_t, PressedKeyState>> pressedKeysSnapshot;
        pressedKeysSnapshot.reserve(manager->_pressedKeys.size());
        for (const auto& entry : manager->_pressedKeys) {
            pressedKeysSnapshot.push_back(entry);
        }

        manager->_pressedKeys.clear();
        manager->_cancelRepeat(std::numeric_limits<uint32_t>::max());
        for (const auto& entry : pressedKeysSnapshot) {
            const PressedKeyState& pressedKey = entry.second;
            manager->_dispatchKey(
                focusedWindow,
                entry.first,
                false,
                pressedKey.key,
                pressedKey.location,
                pressedKey.extraModifiers,
                false
            );
        }
        KeyWayland::clearKeyStates();
    }

    if (manager->_xkbState != nullptr) {
        xkb_state_unref(manager->_xkbState);
    }
    if (manager->_xkbKeymap != nullptr) {
        xkb_keymap_unref(manager->_xkbKeymap);
    }

    manager->_xkbKeymap = keymap;
    manager->_xkbState = state;
    manager->_xkbShiftMod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_SHIFT);
    manager->_xkbControlMod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CTRL);
    manager->_xkbAltMod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_ALT);
    manager->_xkbLogoMod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_LOGO);
    manager->_xkbCapsMod = xkb_keymap_mod_get_index(keymap, XKB_MOD_NAME_CAPS);
    manager->_refreshKeyboardModifiers();
}

void jwm::WindowManagerWayland::onKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_lastInputSerial = serial;
    WindowWayland* window = manager->_windowBySurface(surface);
    manager->_handleKeyboardFocusEnter(window, keys);
}

void jwm::WindowManagerWayland::onKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_handleKeyboardFocusLeave(true);
}

void jwm::WindowManagerWayland::onKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t keycode, uint32_t state) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_lastInputSerial = serial;
    WindowWayland* window = manager->_keyboardFocusWindow;
    if (window == nullptr) {
        return;
    }

    bool isPressed = state == WL_KEYBOARD_KEY_STATE_PRESSED;

    Key key;
    KeyLocation location;
    int extraModifiers;
    manager->_translateKeycode(keycode, key, location, extraModifiers);

    if (isPressed) {
        bool repeats = manager->_shouldRepeatKey(keycode);
        manager->_pressedKeys[keycode] = PressedKeyState {
            key,
            location,
            extraModifiers,
            repeats
        };

        manager->_dispatchKey(window, keycode, true, key, location, extraModifiers, true);

        if (repeats) {
            manager->_scheduleRepeat(keycode, key, location, extraModifiers);
        }
        return;
    }

    auto pressedIt = manager->_pressedKeys.find(keycode);
    if (pressedIt != manager->_pressedKeys.end()) {
        key = pressedIt->second.key;
        location = pressedIt->second.location;
        extraModifiers = pressedIt->second.extraModifiers;
        manager->_pressedKeys.erase(pressedIt);
    }

    manager->_dispatchKey(window, keycode, false, key, location, extraModifiers, false);
    manager->_cancelRepeat(keycode);
}

void jwm::WindowManagerWayland::onKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_lastInputSerial = serial;
    if (manager->_xkbState == nullptr) {
        return;
    }

    xkb_state_update_mask(manager->_xkbState, depressed, latched, locked, 0, 0, group);
    manager->_refreshKeyboardModifiers();
}

void jwm::WindowManagerWayland::onKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay) {
    WindowManagerWayland* manager = static_cast<WindowManagerWayland*>(data);
    manager->_repeatRate = std::max(rate, 0);
    manager->_repeatDelay = std::max(delay, 0);

    if (manager->_repeatRate <= 0) {
        manager->_repeat.isActive = false;
    }
}
