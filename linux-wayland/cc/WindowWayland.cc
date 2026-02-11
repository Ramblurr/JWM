#include <jni.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

#include "AppWayland.hh"
#include "Log.hh"
#include "Window.hh"
#include "impl/Library.hh"
#include "xdg-shell-client-protocol.hh"

namespace jwm {
    static int _createShmFile(size_t size) {
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

    class WindowWayland: public Window {
    public:
        explicit WindowWayland(JNIEnv* env)
            : Window(env)
            , _windowManager(getWaylandWindowManager()) {
        }

        ~WindowWayland() override {
            close();
        }

        IRect getWindowRect() const {
            return _windowRect;
        }

        IRect getContentRect() const {
            return _contentRect;
        }

        void setWindowPosition(int left, int top) {
            _windowRect = IRect::makeXYWH(left, top, _windowRect.getWidth(), _windowRect.getHeight());
            _contentRect = IRect::makeXYWH(left, top, _contentRect.getWidth(), _contentRect.getHeight());
        }

        void setWindowSize(int width, int height) {
            if (width <= 0 || height <= 0) {
                return;
            }
            _pendingWidth = width;
            _pendingHeight = height;
            _windowRect = IRect::makeXYWH(_windowRect.fLeft, _windowRect.fTop, width, height);
            _contentRect = IRect::makeXYWH(_contentRect.fLeft, _contentRect.fTop, width, height);
            if (_xdgSurface != nullptr && _isConfigured) {
                xdg_surface_set_window_geometry(_xdgSurface, 0, 0, width, height);
                wl_surface_commit(_wlSurface);
            }
        }

        void setContentSize(int width, int height) {
            setWindowSize(width, height);
        }

        void setVisible(bool visible) {
            if (_isClosed) {
                return;
            }
            if (_isVisible == visible) {
                return;
            }
            _isVisible = visible;
            if (_isVisible) {
                if (!_ensureSurface()) {
                    _isVisible = false;
                    return;
                }
                wl_surface_commit(_wlSurface);
                wl_display_flush(_windowManager.getDisplay());
                return;
            }
            _destroySurface();
        }

        bool isVisible() const {
            return _isVisible;
        }

        void setFullScreen(bool value) {
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

        bool isFullScreen() const {
            return _isFullScreen;
        }

        void setTitle(const std::string& title) {
            _title = title;
            if (_xdgToplevel != nullptr) {
                xdg_toplevel_set_title(_xdgToplevel, _title.c_str());
                if (_isConfigured) {
                    wl_surface_commit(_wlSurface);
                }
            }
        }

        void setAppId(const std::string& appId) {
            _appId = appId;
            if (_xdgToplevel != nullptr) {
                xdg_toplevel_set_app_id(_xdgToplevel, _appId.c_str());
                if (_isConfigured) {
                    wl_surface_commit(_wlSurface);
                }
            }
        }

        void close() {
            if (_isClosed) {
                return;
            }
            _isClosed = true;
            _isVisible = false;
            _destroySurface();
        }

        jobject getScreen(JNIEnv* env) {
            auto screens = _windowManager.getScreens();
            if (!screens.empty()) {
                return screens.front().asJavaObject(env);
            }
            IRect bounds = IRect::makeXYWH(0, 0, 1920, 1080);
            return classes::Screen::make(env, 1, true, bounds, bounds, 1.f);
        }

        static void _onXdgSurfaceConfigure(void* data, xdg_surface* xdgSurface, uint32_t serial) {
            WindowWayland* instance = static_cast<WindowWayland*>(data);
            instance->_handleXdgSurfaceConfigure(serial);
        }

        static void _onXdgToplevelConfigure(void* data, xdg_toplevel* xdgToplevel, int32_t width, int32_t height, wl_array* states) {
            WindowWayland* instance = static_cast<WindowWayland*>(data);
            instance->_handleXdgToplevelConfigure(width, height);
        }

        static void _onXdgToplevelClose(void* data, xdg_toplevel* xdgToplevel) {
            WindowWayland* instance = static_cast<WindowWayland*>(data);
            instance->dispatch(classes::EventWindowCloseRequest::kInstance);
        }

        void _handleXdgSurfaceConfigure(uint32_t serial) {
            if (_xdgSurface == nullptr || _wlSurface == nullptr) {
                return;
            }
            xdg_surface_ack_configure(_xdgSurface, serial);
            int width = _pendingWidth > 0 ? _pendingWidth : _contentRect.getWidth();
            int height = _pendingHeight > 0 ? _pendingHeight : _contentRect.getHeight();
            if (!_ensureShmBuffer(width, height)) {
                return;
            }
            wl_surface_attach(_wlSurface, _wlBuffer, 0, 0);
            if (_windowManager.getCompositorVersion() >= 4) {
                wl_surface_damage_buffer(_wlSurface, 0, 0, width, height);
            } else {
                wl_surface_damage(_wlSurface, 0, 0, width, height);
            }
            xdg_surface_set_window_geometry(_xdgSurface, 0, 0, width, height);
            _windowRect = IRect::makeXYWH(_windowRect.fLeft, _windowRect.fTop, width, height);
            _contentRect = IRect::makeXYWH(_contentRect.fLeft, _contentRect.fTop, width, height);
            _isConfigured = true;
            wl_surface_commit(_wlSurface);
        }

        void _handleXdgToplevelConfigure(int32_t width, int32_t height) {
            if (width > 0) {
                _pendingWidth = width;
            }
            if (height > 0) {
                _pendingHeight = height;
            }
        }

        bool _ensureSurface() {
            if (_wlSurface != nullptr) {
                return true;
            }
            if (!_windowManager.isReadyForWindows()) {
                JWM_LOG("Wayland: window globals are not ready");
                return false;
            }

            _wlSurface = wl_compositor_create_surface(_windowManager.getCompositor());
            if (_wlSurface == nullptr) {
                JWM_LOG("Wayland: wl_compositor_create_surface failed");
                return false;
            }

            _xdgSurface = xdg_wm_base_get_xdg_surface(_windowManager.getXdgWmBase(), _wlSurface);
            if (_xdgSurface == nullptr) {
                JWM_LOG("Wayland: xdg_wm_base_get_xdg_surface failed");
                _destroySurface();
                return false;
            }
            static xdg_surface_listener xdgSurfaceListener {
                &WindowWayland::_onXdgSurfaceConfigure
            };
            if (xdg_surface_add_listener(_xdgSurface, &xdgSurfaceListener, this) != 0) {
                JWM_LOG("Wayland: xdg_surface_add_listener failed");
                _destroySurface();
                return false;
            }

            _xdgToplevel = xdg_surface_get_toplevel(_xdgSurface);
            if (_xdgToplevel == nullptr) {
                JWM_LOG("Wayland: xdg_surface_get_toplevel failed");
                _destroySurface();
                return false;
            }
            static xdg_toplevel_listener xdgToplevelListener {
                &WindowWayland::_onXdgToplevelConfigure,
                &WindowWayland::_onXdgToplevelClose
            };
            if (xdg_toplevel_add_listener(_xdgToplevel, &xdgToplevelListener, this) != 0) {
                JWM_LOG("Wayland: xdg_toplevel_add_listener failed");
                _destroySurface();
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
            _pendingWidth = _contentRect.getWidth();
            _pendingHeight = _contentRect.getHeight();
            return true;
        }

        bool _ensureShmBuffer(int width, int height) {
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

        void _destroyShmBuffer() {
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

        void _destroySurface() {
            _isConfigured = false;
            _destroyShmBuffer();
            if (_xdgToplevel != nullptr) {
                xdg_toplevel_destroy(_xdgToplevel);
                _xdgToplevel = nullptr;
            }
            if (_xdgSurface != nullptr) {
                xdg_surface_destroy(_xdgSurface);
                _xdgSurface = nullptr;
            }
            if (_wlSurface != nullptr) {
                wl_surface_destroy(_wlSurface);
                _wlSurface = nullptr;
            }
        }

        WindowManagerWayland& _windowManager;
        wl_surface* _wlSurface = nullptr;
        wl_buffer* _wlBuffer = nullptr;
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

        std::string _title;
        std::string _appId;
    };

    static std::string _stringFromJava(JNIEnv* env, jstring str) {
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
