#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <limits>
#include <vector>

#include "ScreenInfoWayland.hh"

struct wl_display;
struct wl_compositor;
struct wl_output;
struct wl_registry;
struct wl_shm;
struct xdg_wm_base;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;

namespace jwm {
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
        uint32_t getCompositorVersion() const;
        bool isReadyForWindows() const;

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

        void _notifyLoop();
        void _drainNotifyPipe();
        void _processTasks();
        void _rebuildScreens();
        bool _initializeNotifyPipe();
        void _cleanup();
        bool _bindXdgOutputManager(wl_registry* registry, uint32_t name, uint32_t version);
        void _bindXdgOutputForOutput(struct WaylandOutputState& outputState);
        void _clearXdgOutputBindings();
        bool _bindCompositor(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindShm(wl_registry* registry, uint32_t name, uint32_t version);
        bool _bindXdgWmBase(wl_registry* registry, uint32_t name, uint32_t version);

        wl_display* _display = nullptr;
        wl_registry* _registry = nullptr;
        wl_compositor* _compositor = nullptr;
        wl_shm* _shm = nullptr;
        xdg_wm_base* _xdgWmBase = nullptr;
        uint32_t _compositorName = std::numeric_limits<uint32_t>::max();
        uint32_t _compositorVersion = 0;
        uint32_t _shmName = std::numeric_limits<uint32_t>::max();
        uint32_t _xdgWmBaseName = std::numeric_limits<uint32_t>::max();
        zxdg_output_manager_v1* _xdgOutputManager = nullptr;
        uint32_t _xdgOutputManagerName = std::numeric_limits<uint32_t>::max();
        bool _runLoop = false;

        int _notifyReadFd = -1;
        int _notifyWriteFd = -1;
        std::atomic_bool _notifyPending { false };

        mutable std::mutex _screensLock;
        std::vector<ScreenInfoWayland> _screens;
        std::map<uint32_t, std::unique_ptr<WaylandOutputState>> _outputByName;

        mutable std::mutex _taskQueueLock;
        std::queue<std::function<void()>> _taskQueue;
    };
}
