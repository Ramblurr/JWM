#include <jni.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <wayland-egl.h>

#include "Log.hh"
#include "WindowWayland.hh"
#include "impl/Library.hh"
#include "impl/RefCounted.hh"

namespace jwm {
    namespace {
        constexpr int kVsyncAdaptive = -1;
        constexpr int kVsyncDisabled = 0;
        constexpr int kVsyncEnabled = 1;

        const char* _eglErrorToString(EGLint error) {
            switch (error) {
                case EGL_SUCCESS: return "EGL_SUCCESS";
                case EGL_NOT_INITIALIZED: return "EGL_NOT_INITIALIZED";
                case EGL_BAD_ACCESS: return "EGL_BAD_ACCESS";
                case EGL_BAD_ALLOC: return "EGL_BAD_ALLOC";
                case EGL_BAD_ATTRIBUTE: return "EGL_BAD_ATTRIBUTE";
                case EGL_BAD_CONTEXT: return "EGL_BAD_CONTEXT";
                case EGL_BAD_CONFIG: return "EGL_BAD_CONFIG";
                case EGL_BAD_CURRENT_SURFACE: return "EGL_BAD_CURRENT_SURFACE";
                case EGL_BAD_DISPLAY: return "EGL_BAD_DISPLAY";
                case EGL_BAD_SURFACE: return "EGL_BAD_SURFACE";
                case EGL_BAD_MATCH: return "EGL_BAD_MATCH";
                case EGL_BAD_PARAMETER: return "EGL_BAD_PARAMETER";
                case EGL_BAD_NATIVE_PIXMAP: return "EGL_BAD_NATIVE_PIXMAP";
                case EGL_BAD_NATIVE_WINDOW: return "EGL_BAD_NATIVE_WINDOW";
                case EGL_CONTEXT_LOST: return "EGL_CONTEXT_LOST";
                default: return "EGL_UNKNOWN_ERROR";
            }
        }

        int _safeDimension(int value) {
            return std::max(1, value);
        }
    }

    class LayerGLWayland: public RefCounted {
    public:
        using EglGetPlatformDisplayExtProc = PFNEGLGETPLATFORMDISPLAYEXTPROC;
        using EglCreatePlatformWindowSurfaceExtProc = PFNEGLCREATEPLATFORMWINDOWSURFACEEXTPROC;

        LayerGLWayland() = default;

        ~LayerGLWayland() override {
            close();
        }

        void attach(WindowWayland* window) {
            if (window == nullptr) {
                throw std::runtime_error("window pointer is null");
            }

            if (_window != nullptr) {
                jwm::unref(&_window);
            }
            _window = jwm::ref(window);

            if (!_initializeEgl()) {
                throw std::runtime_error("failed to initialize EGL display/context");
            }

            IRect contentRect = _window->getContentRect();
            _width = contentRect.getWidth();
            _height = contentRect.getHeight();
            if (!_createOrUpdateSurface()) {
                throw std::runtime_error("failed to create EGL window surface");
            }

            makeCurrent();
            setVsyncMode(kVsyncAdaptive);
        }

        void reconfigure() {
            if (_window == nullptr || _eglDisplay == EGL_NO_DISPLAY || _eglContext == EGL_NO_CONTEXT) {
                return;
            }
            _destroySurface();
            _createOrUpdateSurface();
        }

        void makeCurrent() {
            if (_window == nullptr || _eglDisplay == EGL_NO_DISPLAY || _eglContext == EGL_NO_CONTEXT) {
                return;
            }
            if (!_createOrUpdateSurface()) {
                return;
            }
            if (!eglMakeCurrent(_eglDisplay, _eglSurface, _eglSurface, _eglContext)) {
                EGLint error = eglGetError();
                JWM_LOG("Wayland EGL: eglMakeCurrent failed: " << _eglErrorToString(error));
            }
        }

        void resize(int width, int height) {
            _width = width;
            _height = height;

            if (_window == nullptr) {
                return;
            }

            if (!_createOrUpdateSurface()) {
                return;
            }

            makeCurrent();
            glClearStencil(0);
            glClearColor(0.f, 0.f, 0.f, 1.f);
            glStencilMask(0xffffffffu);
            glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
            glViewport(0, 0, _safeDimension(_width), _safeDimension(_height));
        }

        void swapBuffers() {
            if (_window == nullptr || _eglDisplay == EGL_NO_DISPLAY) {
                return;
            }
            if (!_window->isReadyForEglPresent()) {
                return;
            }
            if (!_createOrUpdateSurface()) {
                return;
            }
            if (!eglSwapBuffers(_eglDisplay, _eglSurface)) {
                EGLint error = eglGetError();
                JWM_LOG("Wayland EGL: eglSwapBuffers failed: " << _eglErrorToString(error));
            }
        }

        void close() {
            _destroySurface();

            if (_eglDisplay != EGL_NO_DISPLAY) {
                eglMakeCurrent(_eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
                if (_eglContext != EGL_NO_CONTEXT) {
                    eglDestroyContext(_eglDisplay, _eglContext);
                    _eglContext = EGL_NO_CONTEXT;
                }
                eglTerminate(_eglDisplay);
                _eglDisplay = EGL_NO_DISPLAY;
            }

            _eglConfig = nullptr;

            if (_window != nullptr) {
                jwm::unref(&_window);
            }

            _width = 0;
            _height = 0;
        }

        void setVsyncMode(int mode) {
            if (_eglDisplay == EGL_NO_DISPLAY) {
                return;
            }

            int interval = 1;
            if (mode == kVsyncDisabled) {
                interval = 0;
            } else if (mode == kVsyncEnabled) {
                interval = 1;
            } else if (mode == kVsyncAdaptive) {
                interval = 1;
            }

            if (!eglSwapInterval(_eglDisplay, interval)) {
                EGLint error = eglGetError();
                JWM_LOG("Wayland EGL: eglSwapInterval(" << interval << ") failed: " << _eglErrorToString(error));
            }
        }

        bool _initializeEgl() {
            if (_eglDisplay != EGL_NO_DISPLAY && _eglContext != EGL_NO_CONTEXT) {
                return true;
            }
            if (_window == nullptr) {
                return false;
            }

            wl_display* wlDisplay = _window->getWindowManager().getDisplay();
            if (wlDisplay == nullptr) {
                return false;
            }

            _eglGetPlatformDisplayExt = reinterpret_cast<EglGetPlatformDisplayExtProc>(
                eglGetProcAddress("eglGetPlatformDisplayEXT")
            );
            _eglCreatePlatformWindowSurfaceExt = reinterpret_cast<EglCreatePlatformWindowSurfaceExtProc>(
                eglGetProcAddress("eglCreatePlatformWindowSurfaceEXT")
            );

            if (_eglGetPlatformDisplayExt != nullptr) {
                _eglDisplay = _eglGetPlatformDisplayExt(EGL_PLATFORM_WAYLAND_KHR, wlDisplay, nullptr);
            }
            if (_eglDisplay == EGL_NO_DISPLAY) {
                _eglDisplay = eglGetDisplay(reinterpret_cast<EGLNativeDisplayType>(wlDisplay));
            }
            if (_eglDisplay == EGL_NO_DISPLAY) {
                return false;
            }

            EGLint major = 0;
            EGLint minor = 0;
            if (!eglInitialize(_eglDisplay, &major, &minor)) {
                _eglDisplay = EGL_NO_DISPLAY;
                return false;
            }

            if (!_createContextForApi(EGL_OPENGL_API, EGL_OPENGL_BIT, false)) {
                if (!_createContextForApi(EGL_OPENGL_ES_API, EGL_OPENGL_ES2_BIT, true)) {
                    eglTerminate(_eglDisplay);
                    _eglDisplay = EGL_NO_DISPLAY;
                    return false;
                }
            }

            return true;
        }

        bool _createContextForApi(EGLenum api, EGLint renderableType, bool gles) {
            if (!eglBindAPI(api)) {
                return false;
            }

            EGLint configAttributes[] = {
                EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
                EGL_RENDERABLE_TYPE, renderableType,
                EGL_RED_SIZE, 8,
                EGL_GREEN_SIZE, 8,
                EGL_BLUE_SIZE, 8,
                EGL_ALPHA_SIZE, 8,
                EGL_DEPTH_SIZE, 24,
                EGL_STENCIL_SIZE, 8,
                EGL_NONE
            };

            EGLConfig config = nullptr;
            EGLint configCount = 0;
            if (!eglChooseConfig(_eglDisplay, configAttributes, &config, 1, &configCount) || configCount == 0) {
                return false;
            }

            EGLint contextAttributes[] = {
                EGL_NONE
            };
            EGLint glesContextAttributes[] = {
                EGL_CONTEXT_CLIENT_VERSION, 2,
                EGL_NONE
            };

            EGLContext context = eglCreateContext(
                _eglDisplay,
                config,
                EGL_NO_CONTEXT,
                gles ? glesContextAttributes : contextAttributes
            );
            if (context == EGL_NO_CONTEXT) {
                return false;
            }

            _eglConfig = config;
            _eglContext = context;
            return true;
        }

        bool _createOrUpdateSurface() {
            if (_window == nullptr || _eglDisplay == EGL_NO_DISPLAY || _eglContext == EGL_NO_CONTEXT || _eglConfig == nullptr) {
                return false;
            }

            int targetWidth = _safeDimension(_width);
            int targetHeight = _safeDimension(_height);

            if (!_window->ensureEglWindow(targetWidth, targetHeight)) {
                return false;
            }
            wl_egl_window* wlEglWindow = _window->getEglWindow();
            uint64_t eglWindowSerial = _window->getEglWindowSerial();
            if (wlEglWindow == nullptr) {
                return false;
            }

            if (_eglSurface != EGL_NO_SURFACE && _eglWindowSerial == eglWindowSerial) {
                _window->resizeEglWindow(targetWidth, targetHeight);
                return true;
            }
            _destroySurface();

            if (_eglCreatePlatformWindowSurfaceExt != nullptr) {
                _eglSurface = _eglCreatePlatformWindowSurfaceExt(
                    _eglDisplay,
                    _eglConfig,
                    wlEglWindow,
                    nullptr
                );
            }
            if (_eglSurface == EGL_NO_SURFACE) {
                _eglSurface = eglCreateWindowSurface(
                    _eglDisplay,
                    _eglConfig,
                    reinterpret_cast<EGLNativeWindowType>(wlEglWindow),
                    nullptr
                );
            }

            if (_eglSurface != EGL_NO_SURFACE) {
                _eglWindowSerial = eglWindowSerial;
            }
            return _eglSurface != EGL_NO_SURFACE;
        }

        void _destroySurface() {
            if (_eglDisplay != EGL_NO_DISPLAY && _eglSurface != EGL_NO_SURFACE) {
                eglDestroySurface(_eglDisplay, _eglSurface);
                _eglSurface = EGL_NO_SURFACE;
            }
            _eglWindowSerial = 0;
        }

        WindowWayland* _window = nullptr;
        EGLDisplay _eglDisplay = EGL_NO_DISPLAY;
        EGLConfig _eglConfig = nullptr;
        EGLContext _eglContext = EGL_NO_CONTEXT;
        EGLSurface _eglSurface = EGL_NO_SURFACE;
        uint64_t _eglWindowSerial = 0;
        int _width = 0;
        int _height = 0;
        EglGetPlatformDisplayExtProc _eglGetPlatformDisplayExt = nullptr;
        EglCreatePlatformWindowSurfaceExtProc _eglCreatePlatformWindowSurfaceExt = nullptr;
    };
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerGL__1nMake
        (JNIEnv* env, jclass cls) {
    jwm::LayerGLWayland* instance = new jwm::LayerGLWayland();
    return reinterpret_cast<jlong>(instance);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nAttach
        (JNIEnv* env, jobject obj, jobject windowObj) {
    try {
        jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
        jwm::WindowWayland* window = reinterpret_cast<jwm::WindowWayland*>(jwm::classes::Native::fromJava(env, windowObj));
        instance->attach(window);
    } catch (const std::exception& e) {
        std::string message = "Failed to init OpenGL (Wayland/EGL): ";
        message += e.what();
        jwm::classes::Throwable::throwLayerNotSupportedException(env, message.c_str());
    }
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nReconfigure
        (JNIEnv* env, jobject obj) {
    jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->reconfigure();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nMakeCurrent
        (JNIEnv* env, jobject obj) {
    jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->makeCurrent();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nResize
        (JNIEnv* env, jobject obj, jint width, jint height) {
    jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->resize(width, height);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nSwapBuffers
        (JNIEnv* env, jobject obj) {
    jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->swapBuffers();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nClose
        (JNIEnv* env, jobject obj) {
    jwm::LayerGLWayland* instance = reinterpret_cast<jwm::LayerGLWayland*>(jwm::classes::Native::fromJava(env, obj));
    instance->close();
}
