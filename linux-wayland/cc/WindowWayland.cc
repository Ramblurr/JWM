#include <jni.h>
#include <memory>

#include "Window.hh"
#include "impl/Library.hh"

namespace jwm {
    class WindowWayland: public Window {
    public:
        explicit WindowWayland(JNIEnv* env): Window(env) {
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
            _windowRect = IRect::makeXYWH(_windowRect.fLeft, _windowRect.fTop, width, height);
            _contentRect = IRect::makeXYWH(_contentRect.fLeft, _contentRect.fTop, width, height);
        }

        void setContentSize(int width, int height) {
            setWindowSize(width, height);
        }

        void setVisible(bool visible) {
            _isVisible = visible;
        }

        bool isVisible() const {
            return _isVisible;
        }

        void setFullScreen(bool value) {
            _isFullScreen = value;
        }

        bool isFullScreen() const {
            return _isFullScreen;
        }

    private:
        IRect _windowRect = IRect::makeXYWH(0, 0, 800, 600);
        IRect _contentRect = IRect::makeXYWH(0, 0, 800, 600);
        bool _isVisible = false;
        bool _isFullScreen = false;
    };
    static jobject makeFallbackScreen(JNIEnv* env) {
        IRect bounds = IRect::makeXYWH(0, 0, 1920, 1080);
        return classes::Screen::make(env, 1, true, bounds, bounds, 1.f);
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
    return jwm::makeFallbackScreen(env);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nRequestFrame
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nClose
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_WindowWayland__1nSetTitle
        (JNIEnv* env, jobject obj, jbyteArray title) {
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
