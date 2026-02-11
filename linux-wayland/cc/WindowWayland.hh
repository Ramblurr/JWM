#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

#include <jni.h>

#include "AppWayland.hh"
#include "Window.hh"
#include "WindowManagerWayland.hh"

struct wl_buffer;
struct wl_callback;
struct wl_surface;
struct wl_array;
struct xdg_surface;
struct xdg_toplevel;

namespace jwm {
    class WindowWayland: public Window {
    public:
        explicit WindowWayland(JNIEnv* env);
        ~WindowWayland() override;

        IRect getWindowRect() const;
        IRect getContentRect() const;
        void setWindowPosition(int left, int top);
        void setWindowSize(int width, int height);
        void setContentSize(int width, int height);
        void setVisible(bool visible);
        bool isVisible() const;
        void setFullScreen(bool value);
        bool isFullScreen() const;
        void setTitle(const std::string& title);
        void setAppId(const std::string& appId);
        void close();
        jobject getScreen(JNIEnv* env);

        void requestFrame();
        bool isReadyForRasterPresent() const;
        bool presentBuffer(wl_buffer* buffer, int width, int height);

        WindowManagerWayland& getWindowManager();

        static void onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial);
        static void onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states);
        static void onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel);
        static void onFrameDone(void* data, wl_callback* callback, uint32_t callbackData);

        void _handleXdgSurfaceConfigure(uint32_t serial);
        void _handleXdgToplevelConfigure(int32_t width, int32_t height);
        void _handleFrameDone(wl_callback* callback);
        bool _armFrameCallbackIfNeeded();
        bool _ensureSurface();
        bool _ensureShmBuffer(int width, int height);
        void _destroyFrameCallback();
        void _destroyShmBuffer();
        void _destroySurface();

        WindowManagerWayland& _windowManager;
        wl_surface* _wlSurface = nullptr;
        wl_buffer* _wlBuffer = nullptr;
        wl_callback* _wlFrameCallback = nullptr;
        xdg_surface* _xdgSurface = nullptr;
        xdg_toplevel* _xdgToplevel = nullptr;

        int _bufferFd = -1;
        void* _bufferData = nullptr;
        size_t _bufferByteCount = 0;
        int _bufferWidth = 0;
        int _bufferHeight = 0;

        IRect _windowRect = IRect::makeXYWH(0, 0, 800, 600);
        IRect _contentRect = IRect::makeXYWH(0, 0, 800, 600);
        int32_t _pendingWidth = 800;
        int32_t _pendingHeight = 600;

        bool _isVisible = false;
        bool _isConfigured = false;
        bool _isFullScreen = false;
        bool _isClosed = false;
        bool _isFrameRequested = false;

        std::string _title;
        std::string _appId;
    };
}
