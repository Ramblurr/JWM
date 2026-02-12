#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <limits>
#include <vector>

#include "Key.hh"
#include "KeyLocation.hh"
#include "MouseCursor.hh"
#include "ScreenInfoWayland.hh"

struct wl_display;
struct wl_compositor;
struct wl_keyboard;
struct wl_output;
struct wl_pointer;
struct wl_registry;
struct wl_array;
struct wl_seat;
struct wl_shm;
struct wl_surface;
struct wl_cursor;
struct wl_cursor_theme;
struct zwp_locked_pointer_v1;
struct zwp_pointer_constraints_v1;
struct zwp_relative_pointer_manager_v1;
struct zwp_relative_pointer_v1;
struct xdg_activation_v1;
struct xdg_activation_token_v1;
struct xdg_wm_base;
struct zxdg_decoration_manager_v1;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

namespace jwm {
    class WindowWayland;
    struct WaylandOutputState;

    class WindowManagerWayland {
    public:
        WindowManagerWayland();
        ~WindowManagerWayland();

        bool connect();
        void runLoop();
        void terminate();

        void enqueueTask(std::function<void()> task);
        std::vector<ScreenInfoWayland> getScreens() const;
        wl_display* getDisplay() const;
        wl_compositor* getCompositor() const;
        wl_shm* getShm() const;
        xdg_wm_base* getXdgWmBase() const;
        zxdg_decoration_manager_v1* getDecorationManager() const;
        uint32_t getCompositorVersion() const;
        bool isReadyForWindows() const;
        void registerWindowSurface(wl_surface* surface, WindowWayland* window);
        void unregisterWindowSurface(wl_surface* surface);
        void requestCursorUpdate(WindowWayland* window);
        void requestPointerLock(WindowWayland* window, bool isLocked);
        bool requestActivation(WindowWayland* window, wl_surface* surface);
        bool isKeyboardFocusedWindow(const WindowWayland* window) const;
        bool tryGetScreenForOutput(wl_output* output, ScreenInfoWayland& screen) const;
        int getOutputScale(wl_output* output) const;
        bool hasOutput(wl_output* output) const;

        static void onRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
        static void onRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
        static void onOutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform);
        static void onOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
        static void onOutputDone(void* data, wl_output* output);
        static void onOutputScale(void* data, wl_output* output, int32_t factor);
        static void onXdgOutputLogicalPosition(void* data, struct zxdg_output_v1* xdgOutput, int32_t x, int32_t y);
        static void onXdgOutputLogicalSize(void* data, struct zxdg_output_v1* xdgOutput, int32_t width, int32_t height);
        static void onXdgOutputDone(void* data, struct zxdg_output_v1* xdgOutput);
        static void onXdgOutputName(void* data, struct zxdg_output_v1* xdgOutput, const char* name);
        static void onXdgOutputDescription(void* data, struct zxdg_output_v1* xdgOutput, const char* description);
        static void onXdgWmBasePing(void* data, struct xdg_wm_base* xdgWmBase, uint32_t serial);
        static void onSeatCapabilities(void* data, wl_seat* seat, uint32_t capabilities);
        static void onSeatName(void* data, wl_seat* seat, const char* name);
        static void onPointerEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface, int32_t sx, int32_t sy);
        static void onPointerLeave(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface);
        static void onPointerMotion(void* data, wl_pointer* pointer, uint32_t time, int32_t sx, int32_t sy);
        static void onPointerButton(void* data, wl_pointer* pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state);
        static void onPointerAxis(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis, int32_t value);
        static void onPointerFrame(void* data, wl_pointer* pointer);
        static void onPointerAxisSource(void* data, wl_pointer* pointer, uint32_t axisSource);
        static void onPointerAxisStop(void* data, wl_pointer* pointer, uint32_t time, uint32_t axis);
        static void onPointerAxisDiscrete(void* data, wl_pointer* pointer, uint32_t axis, int32_t discrete);
        static void onPointerAxisValue120(void* data, wl_pointer* pointer, uint32_t axis, int32_t value120);
        static void onPointerAxisRelativeDirection(void* data, wl_pointer* pointer, uint32_t axis, uint32_t direction);
        static void onRelativePointerMotion(void* data, zwp_relative_pointer_v1* relativePointer, uint32_t utimeHi, uint32_t utimeLo, int32_t dx, int32_t dy, int32_t dxUnaccel, int32_t dyUnaccel);
        static void onLockedPointerLocked(void* data, zwp_locked_pointer_v1* lockedPointer);
        static void onLockedPointerUnlocked(void* data, zwp_locked_pointer_v1* lockedPointer);
        static void onKeyboardKeymap(void* data, wl_keyboard* keyboard, uint32_t format, int32_t fd, uint32_t size);
        static void onKeyboardEnter(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface, wl_array* keys);
        static void onKeyboardLeave(void* data, wl_keyboard* keyboard, uint32_t serial, wl_surface* surface);
        static void onKeyboardKey(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t time, uint32_t key, uint32_t state);
        static void onKeyboardModifiers(void* data, wl_keyboard* keyboard, uint32_t serial, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group);
        static void onKeyboardRepeatInfo(void* data, wl_keyboard* keyboard, int32_t rate, int32_t delay);
        static void onActivationTokenDone(void* data, xdg_activation_token_v1* token, const char* tokenString);

        void _notifyLoop();
        void _drainNotifyPipe();
        void _processTasks();
        void _dispatchRepeatIfNeeded();
        int _getPollTimeoutMillis() const;
        void _rebuildScreens();
        bool _initializeNotifyPipe();
        void _cleanup();
        bool _bindXdgOutputManager(wl_registry* registry, uint32_t name, uint32_t version);
        void _bindXdgOutputForOutput(struct WaylandOutputState& outputState);
        void _clearXdgOutputBindings();
        bool _bindCompositor(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindShm(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindXdgWmBase(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindSeat(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindPointerConstraints(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindRelativePointerManager(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindDecorationManager(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindActivationManager(wl_registry* registry, uint32_t name, uint32_t version);
        void _cancelActivationRequest();
        void _resetPointer();
        void _destroyRelativePointer();
        void _destroyLockedPointer();
        void _updatePointerLock();
        void _resetKeyboard();
        void _resetSeat();
        WindowWayland* _windowBySurface(wl_surface* surface) const;
        void _dispatchMouseMove(WindowWayland* window, int movementX, int movementY);
        void _dispatchMouseButton(WindowWayland* window, MouseButton button, bool isPressed);
        void _dispatchMouseScroll(WindowWayland* window, float deltaX, float deltaY, float deltaChars, float deltaLines);
        void _flushPointerAxis();
        void _handleKeyboardFocusEnter(WindowWayland* window, wl_array* keys);
        void _handleKeyboardFocusLeave(bool dispatchFocusOut);
        void _dispatchKey(WindowWayland* window, uint32_t keycode, bool isPressed, Key key, KeyLocation location, int extraModifiers, bool emitTextInput);
        void _dispatchTextInput(WindowWayland* window, uint32_t keycode);
        bool _translateKeycode(uint32_t keycode, Key& key, KeyLocation& location, int& extraModifiers) const;
        bool _shouldRepeatKey(uint32_t keycode) const;
        void _scheduleRepeat(uint32_t keycode, Key key, KeyLocation location, int extraModifiers);
        void _cancelRepeat(uint32_t keycode);
        void _refreshKeyboardModifiers();
        void _applyCursorForFocus();
        bool _ensureCursorResources();
        wl_cursor* _cursorForType(MouseCursor cursorType);
        void _destroyCursorResources();

        wl_display* _display = nullptr;
        wl_registry* _registry = nullptr;
        wl_compositor* _compositor = nullptr;
        wl_shm* _shm = nullptr;
        xdg_wm_base* _xdgWmBase = nullptr;
        wl_seat* _seat = nullptr;
        wl_pointer* _pointer = nullptr;
        wl_keyboard* _keyboard = nullptr;
        zwp_pointer_constraints_v1* _pointerConstraints = nullptr;
        zwp_relative_pointer_manager_v1* _relativePointerManager = nullptr;
        zwp_relative_pointer_v1* _relativePointer = nullptr;
        zwp_locked_pointer_v1* _lockedPointer = nullptr;
        zxdg_decoration_manager_v1* _decorationManager = nullptr;
        xdg_activation_v1* _activationManager = nullptr;
        xdg_activation_token_v1* _activationToken = nullptr;
        wl_surface* _activationSurface = nullptr;
        WindowWayland* _activationWindow = nullptr;
        uint32_t _compositorName = std::numeric_limits<uint32_t>::max();
        uint32_t _compositorVersion = 0;
        uint32_t _shmName = std::numeric_limits<uint32_t>::max();
        uint32_t _xdgWmBaseName = std::numeric_limits<uint32_t>::max();
        uint32_t _seatName = std::numeric_limits<uint32_t>::max();
        uint32_t _seatVersion = 0;
        uint32_t _pointerConstraintsName = std::numeric_limits<uint32_t>::max();
        uint32_t _relativePointerManagerName = std::numeric_limits<uint32_t>::max();
        uint32_t _decorationManagerName = std::numeric_limits<uint32_t>::max();
        uint32_t _activationManagerName = std::numeric_limits<uint32_t>::max();
        zxdg_output_manager_v1* _xdgOutputManager = nullptr;
        uint32_t _xdgOutputManagerName = std::numeric_limits<uint32_t>::max();
        bool _runLoop = false;

        int _notifyReadFd = -1;
        int _notifyWriteFd = -1;
        std::atomic_bool _notifyPending { false };

        mutable std::mutex _screensLock;
        std::vector<ScreenInfoWayland> _screens;
        std::map<uint32_t, std::unique_ptr<WaylandOutputState>> _outputByName;
        std::map<wl_surface*, WindowWayland*> _surfaceToWindow;
        std::map<WindowWayland*, bool> _pointerLockRequests;
        WindowWayland* _pointerFocusWindow = nullptr;
        WindowWayland* _keyboardFocusWindow = nullptr;
        uint32_t _lastInputSerial = 0;
        WindowWayland* _pointerLockWindow = nullptr;
        bool _isPointerLockActive = false;
        uint32_t _pointerEnterSerial = 0;
        int _pointerContentX = 0;
        int _pointerContentY = 0;
        int _pointerButtonMask = 0;
        bool _pointerAxisPending = false;
        bool _pointerAxisDiscretePending = false;
        bool _pointerAxisValue120Pending = false;
        double _pointerAxisX = 0.0;
        double _pointerAxisY = 0.0;
        double _pointerAxisDiscreteX = 0.0;
        double _pointerAxisDiscreteY = 0.0;
        double _pointerAxisValue120X = 0.0;
        double _pointerAxisValue120Y = 0.0;

        xkb_context* _xkbContext = nullptr;
        xkb_keymap* _xkbKeymap = nullptr;
        xkb_state* _xkbState = nullptr;
        uint32_t _xkbShiftMod = std::numeric_limits<uint32_t>::max();
        uint32_t _xkbControlMod = std::numeric_limits<uint32_t>::max();
        uint32_t _xkbAltMod = std::numeric_limits<uint32_t>::max();
        uint32_t _xkbLogoMod = std::numeric_limits<uint32_t>::max();
        uint32_t _xkbCapsMod = std::numeric_limits<uint32_t>::max();
        int _keyboardModifiers = 0;

        struct PressedKeyState {
            Key key = Key::UNDEFINED;
            KeyLocation location = KeyLocation::DEFAULT;
            int extraModifiers = 0;
            bool repeats = false;
        };
        std::map<uint32_t, PressedKeyState> _pressedKeys;

        struct RepeatState {
            bool isActive = false;
            uint32_t keycode = 0;
            Key key = Key::UNDEFINED;
            KeyLocation location = KeyLocation::DEFAULT;
            int extraModifiers = 0;
            std::chrono::steady_clock::time_point nextAt;
        };
        RepeatState _repeat;
        int32_t _repeatRate = 0;
        int32_t _repeatDelay = 0;
        bool _isHandlingKeyboardFocusLeave = false;

        WindowWayland* _pendingCursorWindow = nullptr;
        wl_surface* _cursorSurface = nullptr;
        wl_cursor_theme* _cursorTheme = nullptr;
        std::map<MouseCursor, wl_cursor*> _cursorByType;
        int _cursorThemeSize = 24;

        mutable std::mutex _taskQueueLock;
        std::queue<std::function<void()>> _taskQueue;
    };
}
