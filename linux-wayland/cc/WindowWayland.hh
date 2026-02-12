#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <jni.h>

#include "AppWayland.hh"
#include "MouseCursor.hh"
#include "Window.hh"
#include "WindowManagerWayland.hh"

struct wl_buffer;
struct wl_callback;
struct wl_egl_window;
struct wl_surface;
struct wl_output;
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
        bool isReadyForEglPresent() const;
        bool presentBuffer(wl_buffer* buffer, int width, int height);
        bool ensureEglWindow(int width, int height);
        void resizeEglWindow(int width, int height);
        int getBufferScale() const;
        wl_egl_window* getEglWindow() const;
        wl_surface* getSurface() const;
        uint64_t getEglWindowSerial() const;
        void toContentPixels(double logicalX, double logicalY, int& x, int& y) const;
        int toContentPixels(double logicalValue) const;
        bool hasEnteredOutput(wl_output* output) const;
        void handleOutputMetricsChanged(wl_output* output);

        WindowManagerWayland& getWindowManager();

        static void onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial);
        static void onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states);
        static void onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel);
        static void onFrameDone(void* data, wl_callback* callback, uint32_t callbackData);
        static void onShowHideSyncDone(void* data, wl_callback* callback, uint32_t callbackData);
        static void onSurfaceEnter(void* data, wl_surface* surface, wl_output* output);
        static void onSurfaceLeave(void* data, wl_surface* surface, wl_output* output);
#if defined(WL_SURFACE_PREFERRED_BUFFER_SCALE_SINCE_VERSION)
        static void onSurfacePreferredBufferScale(void* data, wl_surface* surface, int32_t factor);
        static void onSurfacePreferredBufferTransform(void* data, wl_surface* surface, uint32_t transform);
#endif

        void _handleXdgSurfaceConfigure(uint32_t serial);
        void _handleXdgToplevelConfigure(int32_t width, int32_t height);
        void _handleFrameDone(wl_callback* callback);
        void _handleSurfaceEnter(wl_output* output);
        void _handleSurfaceLeave(wl_output* output);
        bool _refreshScreenAssociation();
        bool _updateBufferScaleFromOutputs();
        int _resolveEnteredOutputScale() const;
        bool _armFrameCallbackIfNeeded();
        bool _ensureSurface();
        void _waitForShowHideSyncIfNeeded();
        void _queueShowHideSync();
        int _resolveBufferScale() const;
        int _toBufferPixels(int logicalValue) const;
        bool _ensureShmBuffer(int width, int height);
        void _destroyShowHideSyncCallback();
        void _destroyEglWindow();
        void _destroyFrameCallback();
        void _destroyShmBuffer();
        void _destroyRoleObjects();
        void _destroySurface();

        WindowManagerWayland& _windowManager;
        wl_surface* _wlSurface = nullptr;
        wl_egl_window* _wlEglWindow = nullptr;
        wl_buffer* _wlBuffer = nullptr;
        wl_callback* _wlFrameCallback = nullptr;
        wl_callback* _showHideSyncCallback = nullptr;
        xdg_surface* _xdgSurface = nullptr;
        xdg_toplevel* _xdgToplevel = nullptr;

        int _bufferFd = -1;
        void* _bufferData = nullptr;
        size_t _bufferByteCount = 0;
        int _bufferWidth = 0;
        int _bufferHeight = 0;
        int _eglWindowWidth = 0;
        int _eglWindowHeight = 0;
        uint64_t _eglWindowSerial = 0;

        IRect _windowRect = IRect::makeXYWH(0, 0, 800, 600);
        IRect _contentRect = IRect::makeXYWH(0, 0, 800, 600);
        IRect _logicalContentRect = IRect::makeXYWH(0, 0, 800, 600);
        int32_t _pendingWidth = 800;
        int32_t _pendingHeight = 600;
        int _bufferScale = 1;
        uint32_t _lastConfigureSerial = 0;

        bool _isVisible = false;
        bool _isConfigured = false;
        bool _isFullScreen = false;
        bool _isClosed = false;
        bool _isFrameRequested = false;
        bool _showHideSyncRequired = false;
        long _screenId = std::numeric_limits<long>::min();
        std::vector<wl_output*> _enteredOutputs;

        std::string _title;
        std::string _appId;
        jwm::MouseCursor _mouseCursor = jwm::MouseCursor::ARROW;
    };
}
