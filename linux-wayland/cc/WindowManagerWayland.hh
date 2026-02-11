#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

#include "ScreenInfoWayland.hh"

struct wl_display;
struct wl_output;
struct wl_registry;

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

        static void onRegistryGlobal(void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version);
        static void onRegistryGlobalRemove(void* data, wl_registry* registry, uint32_t name);
        static void onOutputGeometry(void* data, wl_output* output, int32_t x, int32_t y, int32_t physicalWidth, int32_t physicalHeight, int32_t subpixel, const char* make, const char* model, int32_t transform);
        static void onOutputMode(void* data, wl_output* output, uint32_t flags, int32_t width, int32_t height, int32_t refresh);
        static void onOutputDone(void* data, wl_output* output);
        static void onOutputScale(void* data, wl_output* output, int32_t factor);

    private:
        void notifyLoop();
        void drainNotifyPipe();
        void processTasks();
        void rebuildScreens();
        bool initializeNotifyPipe();
        void cleanup();

        wl_display* _display = nullptr;
        wl_registry* _registry = nullptr;
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
