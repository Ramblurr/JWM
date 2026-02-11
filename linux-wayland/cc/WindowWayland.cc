#include <jni.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <algorithm>
#include <cmath>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>
#include <wayland-egl.h>

#include "Log.hh"
#include "WindowWayland.hh"
#include "impl/JNILocal.hh"
#include "impl/Library.hh"
#include "xdg-shell-client-protocol.hh"

namespace jwm {
    namespace {
        int _createShmFile(size_t size) {
            char name[] = "/tmp/jwm-wayland-XXXXXX";
            int fd = mkstemp(name);
            if (fd < 0) {
                return -1;
            }
            unlink(name);
            if (ftruncate(fd, static_cast<off_t>(size)) != 0) {
                close(fd);
                return -1;
            }
            return fd;
        }

        xdg_surface_listener kXdgSurfaceListener {
            &WindowWayland::onXdgSurfaceConfigure
        };

        xdg_toplevel_listener kXdgToplevelListener {
            &WindowWayland::onXdgToplevelConfigure,
            &WindowWayland::onXdgToplevelClose
        };

        wl_callback_listener kFrameCallbackListener {
            &WindowWayland::onFrameDone
        };

        wl_callback_listener kShowHideSyncCallbackListener {
            &WindowWayland::onShowHideSyncDone
        };

        std::string _stringFromJava(JNIEnv* env, jstring str) {
            if (str == nullptr) {
                return "";
            }
            const char* chars = env->GetStringUTFChars(str, nullptr);
            if (chars == nullptr) {
                return "";
            }
            std::string result(chars);
            env->ReleaseStringUTFChars(str, chars);
            return result;
        }
    }
}

jwm::WindowWayland::WindowWayland(JNIEnv* env)
    : Window(env)
    , _windowManager(getWaylandWindowManager()) {
}

jwm::WindowWayland::~WindowWayland() {
    close();
}

jwm::IRect jwm::WindowWayland::getWindowRect() const {
    return _windowRect;
}

jwm::IRect jwm::WindowWayland::getContentRect() const {
    return _contentRect;
}

void jwm::WindowWayland::setWindowPosition(int left, int top) {
    _windowRect = IRect::makeXYWH(left, top, _windowRect.getWidth(), _windowRect.getHeight());
    _logicalContentRect = IRect::makeXYWH(left, top, _logicalContentRect.getWidth(), _logicalContentRect.getHeight());
    _contentRect = IRect::makeXYWH(left, top, _contentRect.getWidth(), _contentRect.getHeight());
}

void jwm::WindowWayland::setWindowSize(int width, int height) {
    if (width <= 0 || height <= 0) {
        return;
    }

    _pendingWidth = width;
    _pendingHeight = height;
    _windowRect = IRect::makeXYWH(_windowRect.fLeft, _windowRect.fTop, width, height);
    _logicalContentRect = IRect::makeXYWH(_logicalContentRect.fLeft, _logicalContentRect.fTop, width, height);
    _contentRect = IRect::makeXYWH(_contentRect.fLeft, _contentRect.fTop, _toBufferPixels(width), _toBufferPixels(height));

    if (_xdgSurface != nullptr && _isConfigured) {
        xdg_surface_set_window_geometry(_xdgSurface, 0, 0, width, height);
        wl_surface_commit(_wlSurface);
    }
}

void jwm::WindowWayland::setContentSize(int width, int height) {
    setWindowSize(width, height);
}

void jwm::WindowWayland::setVisible(bool visible) {
    if (_isClosed) {
        return;
    }
    if (_isVisible == visible) {
        return;
    }

    _isVisible = visible;
    if (_isVisible) {
        _waitForShowHideSyncIfNeeded();
        if (!_ensureSurface()) {
            _isVisible = false;
            return;
        }
        wl_surface_commit(_wlSurface);
        wl_display_flush(_windowManager.getDisplay());
        _queueShowHideSync();
        return;
    }

    _waitForShowHideSyncIfNeeded();
    _isConfigured = false;
    _isFrameRequested = false;
    _destroyFrameCallback();
    _destroyRoleObjects();
    if (_wlSurface != nullptr) {
        wl_surface_attach(_wlSurface, nullptr, 0, 0);
        wl_surface_commit(_wlSurface);
        wl_display_flush(_windowManager.getDisplay());
    }
    _queueShowHideSync();
}

bool jwm::WindowWayland::isVisible() const {
    return _isVisible;
}

void jwm::WindowWayland::setFullScreen(bool value) {
    _isFullScreen = value;
    if (_xdgToplevel == nullptr) {
        return;
    }

    if (value) {
        xdg_toplevel_set_fullscreen(_xdgToplevel, nullptr);
    } else {
        xdg_toplevel_unset_fullscreen(_xdgToplevel);
    }

    if (_isConfigured) {
        wl_surface_commit(_wlSurface);
    }
}

bool jwm::WindowWayland::isFullScreen() const {
    return _isFullScreen;
}

void jwm::WindowWayland::setTitle(const std::string& title) {
    _title = title;
    if (_xdgToplevel != nullptr) {
        xdg_toplevel_set_title(_xdgToplevel, _title.c_str());
        if (_isConfigured) {
            wl_surface_commit(_wlSurface);
        }
    }
}

void jwm::WindowWayland::setAppId(const std::string& appId) {
    _appId = appId;
    if (_xdgToplevel != nullptr) {
        xdg_toplevel_set_app_id(_xdgToplevel, _appId.c_str());
        if (_isConfigured) {
            wl_surface_commit(_wlSurface);
        }
    }
}

void jwm::WindowWayland::close() {
    if (_isClosed) {
        return;
    }

    _isClosed = true;
    _isVisible = false;
    _isFrameRequested = false;
    _destroySurface();
}

jobject jwm::WindowWayland::getScreen(JNIEnv* env) {
    auto screens = _windowManager.getScreens();
    if (!screens.empty()) {
        return screens.front().asJavaObject(env);
    }

    IRect bounds = IRect::makeXYWH(0, 0, 1920, 1080);
    return classes::Screen::make(env, 1, true, bounds, bounds, 1.f);
}

void jwm::WindowWayland::requestFrame() {
    if (_isClosed || !_isVisible || _wlSurface == nullptr) {
        return;
    }

    _isFrameRequested = true;
    if (_armFrameCallbackIfNeeded()) {
        wl_surface_commit(_wlSurface);
    }
}

bool jwm::WindowWayland::isReadyForRasterPresent() const {
    return !_isClosed && _isVisible && _isConfigured && _wlSurface != nullptr;
}

bool jwm::WindowWayland::isReadyForEglPresent() const {
    return !_isClosed && _isVisible && _isConfigured && _wlSurface != nullptr && _wlEglWindow != nullptr;
}

bool jwm::WindowWayland::presentBuffer(wl_buffer* buffer, int width, int height) {
    if (!isReadyForRasterPresent() || buffer == nullptr || width <= 0 || height <= 0) {
        return false;
    }

    wl_surface_attach(_wlSurface, buffer, 0, 0);
    if (_windowManager.getCompositorVersion() >= 4) {
        wl_surface_damage_buffer(_wlSurface, 0, 0, width, height);
    } else {
        wl_surface_damage(_wlSurface, 0, 0, width, height);
    }
    _armFrameCallbackIfNeeded();
    wl_surface_commit(_wlSurface);
    return true;
}

bool jwm::WindowWayland::ensureEglWindow(int width, int height) {
    if (_isClosed) {
        return false;
    }
    if (!_ensureSurface()) {
        return false;
    }

    int targetWidth = std::max(width, 1);
    int targetHeight = std::max(height, 1);
    if (_wlEglWindow == nullptr) {
        _wlEglWindow = wl_egl_window_create(_wlSurface, targetWidth, targetHeight);
        if (_wlEglWindow == nullptr) {
            JWM_LOG("Wayland: wl_egl_window_create failed");
            return false;
        }
        _eglWindowSerial++;
    } else if (_eglWindowWidth != targetWidth || _eglWindowHeight != targetHeight) {
        wl_egl_window_resize(_wlEglWindow, targetWidth, targetHeight, 0, 0);
    }

    _eglWindowWidth = targetWidth;
    _eglWindowHeight = targetHeight;
    return true;
}

void jwm::WindowWayland::resizeEglWindow(int width, int height) {
    if (_wlEglWindow == nullptr) {
        return;
    }
    int targetWidth = std::max(width, 1);
    int targetHeight = std::max(height, 1);
    if (_eglWindowWidth == targetWidth && _eglWindowHeight == targetHeight) {
        return;
    }
    wl_egl_window_resize(_wlEglWindow, targetWidth, targetHeight, 0, 0);
    _eglWindowWidth = targetWidth;
    _eglWindowHeight = targetHeight;
}

int jwm::WindowWayland::getBufferScale() const {
    return _bufferScale;
}

wl_egl_window* jwm::WindowWayland::getEglWindow() const {
    return _wlEglWindow;
}

wl_surface* jwm::WindowWayland::getSurface() const {
    return _wlSurface;
}

uint64_t jwm::WindowWayland::getEglWindowSerial() const {
    return _eglWindowSerial;
}

jwm::WindowManagerWayland& jwm::WindowWayland::getWindowManager() {
    return _windowManager;
}

void jwm::WindowWayland::onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial) {
    WindowWayland* instance = static_cast<WindowWayland*>(data);
    instance->_handleXdgSurfaceConfigure(serial);
}

void jwm::WindowWayland::onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states) {
    WindowWayland* instance = static_cast<WindowWayland*>(data);
    instance->_handleXdgToplevelConfigure(width, height);
}

void jwm::WindowWayland::onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel) {
    WindowWayland* instance = static_cast<WindowWayland*>(data);
    instance->dispatch(classes::EventWindowCloseRequest::kInstance);
}

void jwm::WindowWayland::onFrameDone(void* data, wl_callback* callback, uint32_t callbackData) {
    WindowWayland* instance = static_cast<WindowWayland*>(data);
    instance->_handleFrameDone(callback);
}

void jwm::WindowWayland::onShowHideSyncDone(void* data, wl_callback* callback, uint32_t callbackData) {
    WindowWayland* instance = static_cast<WindowWayland*>(data);
    if (instance->_showHideSyncCallback != callback) {
        if (callback != nullptr) {
            wl_callback_destroy(callback);
        }
        return;
    }

    instance->_destroyShowHideSyncCallback();
    instance->_showHideSyncRequired = false;
}

void jwm::WindowWayland::_handleXdgSurfaceConfigure(uint32_t serial) {
    if (_xdgSurface == nullptr || _wlSurface == nullptr) {
        return;
    }

    // Ignore exact duplicate configure serials; each serial should be handled once.
    if (_lastConfigureSerial != 0 && serial == _lastConfigureSerial) {
        return;
    }

    xdg_surface_ack_configure(_xdgSurface, serial);
    _lastConfigureSerial = serial;

    int width = _pendingWidth > 0 ? _pendingWidth : _logicalContentRect.getWidth();
    int height = _pendingHeight > 0 ? _pendingHeight : _logicalContentRect.getHeight();
    int previousWindowWidth = _windowRect.getWidth();
    int previousWindowHeight = _windowRect.getHeight();
    int previousContentWidth = _contentRect.getWidth();
    int previousContentHeight = _contentRect.getHeight();
    bool isFirstConfigure = !_isConfigured;
    bool dispatchInitialFrame = false;
    int nextBufferScale = _resolveBufferScale();
    bool scaleChanged = nextBufferScale != _bufferScale;
    _bufferScale = nextBufferScale;

    int contentWidth = _toBufferPixels(width);
    int contentHeight = _toBufferPixels(height);

    if (isFirstConfigure) {
        if (!_ensureShmBuffer(contentWidth, contentHeight)) {
            return;
        }
        wl_surface_attach(_wlSurface, _wlBuffer, 0, 0);
        if (_windowManager.getCompositorVersion() >= 4) {
            wl_surface_damage_buffer(_wlSurface, 0, 0, contentWidth, contentHeight);
        } else {
            wl_surface_damage(_wlSurface, 0, 0, width, height);
        }
    }

    if (scaleChanged) {
        wl_surface_set_buffer_scale(_wlSurface, _bufferScale);
    }

    xdg_surface_set_window_geometry(_xdgSurface, 0, 0, width, height);
    _windowRect = IRect::makeXYWH(_windowRect.fLeft, _windowRect.fTop, width, height);
    _logicalContentRect = IRect::makeXYWH(_logicalContentRect.fLeft, _logicalContentRect.fTop, width, height);
    _contentRect = IRect::makeXYWH(_contentRect.fLeft, _contentRect.fTop, contentWidth, contentHeight);
    _isConfigured = true;
    if (isFirstConfigure) {
        dispatchInitialFrame = true;
    }
    if (isFirstConfigure && !_isFrameRequested) {
        _isFrameRequested = true;
    }
    resizeEglWindow(contentWidth, contentHeight);
    _armFrameCallbackIfNeeded();
    wl_surface_commit(_wlSurface);

    bool sizeChanged = width != previousWindowWidth || height != previousWindowHeight ||
        contentWidth != previousContentWidth || contentHeight != previousContentHeight;

    if (sizeChanged) {
        JNILocal<jobject> eventWindowResize(fEnv,
            classes::EventWindowResize::make(fEnv, width, height, contentWidth, contentHeight));
        dispatch(eventWindowResize.get());
    }

    // schedule an immediate redraw on configure/resize so compositors can apply the newly configured geometry with a fresh buffer.
    if ((dispatchInitialFrame || sizeChanged) && !_isClosed && _isVisible) {
        dispatch(classes::EventFrame::kInstance);
    }
}

void jwm::WindowWayland::_handleXdgToplevelConfigure(int32_t width, int32_t height) {
    if (width > 0) {
        _pendingWidth = width;
    }
    if (height > 0) {
        _pendingHeight = height;
    }
}

void jwm::WindowWayland::_handleFrameDone(wl_callback* callback) {
    if (_wlFrameCallback != callback) {
        if (callback != nullptr) {
            wl_callback_destroy(callback);
        }
        return;
    }

    _destroyFrameCallback();

    if (!_isFrameRequested) {
        return;
    }

    _isFrameRequested = false;
    if (!isReadyForRasterPresent()) {
        return;
    }

    dispatch(classes::EventFrame::kInstance);
}

bool jwm::WindowWayland::_armFrameCallbackIfNeeded() {
    if (!isReadyForRasterPresent() || !_isFrameRequested || _wlFrameCallback != nullptr) {
        return false;
    }

    _wlFrameCallback = wl_surface_frame(_wlSurface);
    if (_wlFrameCallback == nullptr) {
        JWM_LOG("Wayland: wl_surface_frame failed");
        return false;
    }
    if (wl_callback_add_listener(_wlFrameCallback, &kFrameCallbackListener, this) != 0) {
        JWM_LOG("Wayland: wl_callback_add_listener failed");
        wl_callback_destroy(_wlFrameCallback);
        _wlFrameCallback = nullptr;
        return false;
    }

    return true;
}

bool jwm::WindowWayland::_ensureSurface() {
    if (!_windowManager.isReadyForWindows()) {
        JWM_LOG("Wayland: window globals are not ready");
        return false;
    }

    if (_wlSurface == nullptr) {
        _wlSurface = wl_compositor_create_surface(_windowManager.getCompositor());
        if (_wlSurface == nullptr) {
            JWM_LOG("Wayland: wl_compositor_create_surface failed");
            return false;
        }
        _bufferScale = _resolveBufferScale();
        wl_surface_set_buffer_scale(_wlSurface, _bufferScale);
    }

    if (_xdgSurface != nullptr && _xdgToplevel != nullptr) {
        return true;
    }

    _xdgSurface = xdg_wm_base_get_xdg_surface(_windowManager.getXdgWmBase(), _wlSurface);
    if (_xdgSurface == nullptr) {
        JWM_LOG("Wayland: xdg_wm_base_get_xdg_surface failed");
        _destroyRoleObjects();
        return false;
    }
    if (xdg_surface_add_listener(_xdgSurface, &kXdgSurfaceListener, this) != 0) {
        JWM_LOG("Wayland: xdg_surface_add_listener failed");
        _destroyRoleObjects();
        return false;
    }

    _xdgToplevel = xdg_surface_get_toplevel(_xdgSurface);
    if (_xdgToplevel == nullptr) {
        JWM_LOG("Wayland: xdg_surface_get_toplevel failed");
        _destroyRoleObjects();
        return false;
    }
    if (xdg_toplevel_add_listener(_xdgToplevel, &kXdgToplevelListener, this) != 0) {
        JWM_LOG("Wayland: xdg_toplevel_add_listener failed");
        _destroyRoleObjects();
        return false;
    }

    if (!_title.empty()) {
        xdg_toplevel_set_title(_xdgToplevel, _title.c_str());
    }
    if (!_appId.empty()) {
        xdg_toplevel_set_app_id(_xdgToplevel, _appId.c_str());
    }
    if (_isFullScreen) {
        xdg_toplevel_set_fullscreen(_xdgToplevel, nullptr);
    }

    _isConfigured = false;
    _pendingWidth = _logicalContentRect.getWidth();
    _pendingHeight = _logicalContentRect.getHeight();
    return true;
}

void jwm::WindowWayland::_waitForShowHideSyncIfNeeded() {
    if (!_showHideSyncRequired) {
        return;
    }

    if (wl_display_roundtrip(_windowManager.getDisplay()) < 0) {
        JWM_LOG("Wayland: show/hide sync roundtrip failed");
    }
    _showHideSyncRequired = false;
}

void jwm::WindowWayland::_queueShowHideSync() {
    _destroyShowHideSyncCallback();
    _showHideSyncRequired = true;
    _showHideSyncCallback = wl_display_sync(_windowManager.getDisplay());
    if (_showHideSyncCallback == nullptr) {
        JWM_LOG("Wayland: wl_display_sync failed for show/hide guard");
        _showHideSyncRequired = false;
        return;
    }
    if (wl_callback_add_listener(_showHideSyncCallback, &kShowHideSyncCallbackListener, this) != 0) {
        JWM_LOG("Wayland: wl_callback_add_listener failed for show/hide guard");
        _destroyShowHideSyncCallback();
        _showHideSyncRequired = false;
    }
}

int jwm::WindowWayland::_resolveBufferScale() const {
    auto screens = _windowManager.getScreens();
    if (screens.empty()) {
        return 1;
    }
    float scale = screens.front().scale;
    if (scale < 1.f) {
        scale = 1.f;
    }
    return std::max(1, static_cast<int>(std::lround(scale)));
}

int jwm::WindowWayland::_toBufferPixels(int logicalValue) const {
    return std::max(logicalValue, 1) * std::max(_bufferScale, 1);
}

bool jwm::WindowWayland::_ensureShmBuffer(int width, int height) {
    if (width <= 0 || height <= 0) {
        return false;
    }

    if (_wlBuffer != nullptr && width == _bufferWidth && height == _bufferHeight) {
        return true;
    }

    _destroyShmBuffer();

    size_t rowBytes = static_cast<size_t>(width) * 4;
    size_t byteCount = rowBytes * static_cast<size_t>(height);
    int fd = _createShmFile(byteCount);
    if (fd < 0) {
        JWM_LOG("Wayland: failed to create shm file");
        return false;
    }

    void* mapped = mmap(nullptr, byteCount, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (mapped == MAP_FAILED) {
        JWM_LOG("Wayland: mmap failed for shm buffer");
        ::close(fd);
        return false;
    }

    std::memset(mapped, 0xCC, byteCount);

    wl_shm_pool* shmPool = wl_shm_create_pool(_windowManager.getShm(), fd, static_cast<int32_t>(byteCount));
    if (shmPool == nullptr) {
        JWM_LOG("Wayland: wl_shm_create_pool failed");
        munmap(mapped, byteCount);
        ::close(fd);
        return false;
    }

    wl_buffer* buffer = wl_shm_pool_create_buffer(
        shmPool,
        0,
        width,
        height,
        static_cast<int32_t>(rowBytes),
        WL_SHM_FORMAT_XRGB8888
    );
    wl_shm_pool_destroy(shmPool);
    if (buffer == nullptr) {
        JWM_LOG("Wayland: wl_shm_pool_create_buffer failed");
        munmap(mapped, byteCount);
        ::close(fd);
        return false;
    }

    _bufferFd = fd;
    _bufferData = mapped;
    _bufferByteCount = byteCount;
    _bufferWidth = width;
    _bufferHeight = height;
    _wlBuffer = buffer;
    return true;
}

void jwm::WindowWayland::_destroyFrameCallback() {
    if (_wlFrameCallback != nullptr) {
        wl_callback_destroy(_wlFrameCallback);
        _wlFrameCallback = nullptr;
    }
}

void jwm::WindowWayland::_destroyShowHideSyncCallback() {
    if (_showHideSyncCallback != nullptr) {
        wl_callback_destroy(_showHideSyncCallback);
        _showHideSyncCallback = nullptr;
    }
}

void jwm::WindowWayland::_destroyEglWindow() {
    if (_wlEglWindow != nullptr) {
        wl_egl_window_destroy(_wlEglWindow);
        _wlEglWindow = nullptr;
        _eglWindowSerial++;
    }
    _eglWindowWidth = 0;
    _eglWindowHeight = 0;
}

void jwm::WindowWayland::_destroyShmBuffer() {
    if (_wlBuffer != nullptr) {
        wl_buffer_destroy(_wlBuffer);
        _wlBuffer = nullptr;
    }
    if (_bufferData != nullptr) {
        munmap(_bufferData, _bufferByteCount);
        _bufferData = nullptr;
    }
    if (_bufferFd >= 0) {
        ::close(_bufferFd);
        _bufferFd = -1;
    }

    _bufferByteCount = 0;
    _bufferWidth = 0;
    _bufferHeight = 0;
}

void jwm::WindowWayland::_destroySurface() {
    _isConfigured = false;
    _isFrameRequested = false;
    _bufferScale = 1;
    _showHideSyncRequired = false;
    _destroyShowHideSyncCallback();
    _destroyEglWindow();
    _destroyFrameCallback();
    _destroyShmBuffer();
    _destroyRoleObjects();
    if (_wlSurface != nullptr) {
        wl_surface_destroy(_wlSurface);
        _wlSurface = nullptr;
    }
}

void jwm::WindowWayland::_destroyRoleObjects() {
    _lastConfigureSerial = 0;
    if (_xdgToplevel != nullptr) {
        xdg_toplevel_destroy(_xdgToplevel);
        _xdgToplevel = nullptr;
    }
    if (_xdgSurface != nullptr) {
        xdg_surface_destroy(_xdgSurface);
        _xdgSurface = nullptr;
    }
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nMake
        (JNIEnv* env, jclass cls) {
    std::unique_ptr<jwm::WindowWayland> instance(new jwm::WindowWayland(env));
    return reinterpret_cast<jlong>(instance.release());
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetVisible
        (JNIEnv* env, jobject obj, jboolean isVisible) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setVisible(isVisible);
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nGetWindowRect
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    return jwm::classes::IRect::toJava(env, instance->getWindowRect());
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nGetContentRect
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    return jwm::classes::IRect::toJava(env, instance->getContentRect());
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetWindowPosition
        (JNIEnv* env, jobject obj, jint left, jint top) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setWindowPosition(left, top);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetWindowSize
        (JNIEnv* env, jobject obj, jint width, jint height) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setWindowSize(width, height);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetContentSize
        (JNIEnv* env, jobject obj, jint width, jint height) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setContentSize(width, height);
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nGetScreen
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    return instance->getScreen(env);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nRequestFrame
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->requestFrame();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nClose
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->close();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetTitle
        (JNIEnv* env, jobject obj, jstring title) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setTitle(jwm::_stringFromJava(env, title));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetAppId
        (JNIEnv* env, jobject obj, jstring appId) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setAppId(jwm::_stringFromJava(env, appId));
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetMouseCursor
        (JNIEnv* env, jobject obj, jint cursorId) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nMaximize
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nMinimize
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nRestore
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetFullScreen
        (JNIEnv* env, jobject obj, jboolean isFullScreen) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->setFullScreen(isFullScreen);
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nIsFullScreen
        (JNIEnv* env, jobject obj) {
    jwm::WindowWayland* instance = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, obj));
    return instance->isFullScreen();
}
