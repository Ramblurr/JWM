#include <jni.h>
#include <vector>

#include "WindowManagerWayland.hh"
#include "impl/Library.hh"

namespace jwm {
    class AppWayland {
    public:
        void init(JNIEnv* env) {
            _jniEnv = env;
        }

        void start() {
            if (!_windowManager.connect()) {
                classes::Throwable::throwRuntimeException(_jniEnv, "Failed to initialize Wayland display connection");
                return;
            }
            _windowManager.runLoop();
        }

        void terminate() {
            _windowManager.terminate();
        }

        void enqueueCallback(jobject callbackRef) {
            _windowManager.enqueueTask([this, callbackRef]() {
                classes::Runnable::run(_jniEnv, callbackRef);
                _jniEnv->DeleteGlobalRef(callbackRef);
            });
        }

        std::vector<ScreenInfoWayland> getScreens() const {
            return _windowManager.getScreens();
        }

    private:
        JNIEnv* _jniEnv = nullptr;
        WindowManagerWayland _windowManager;
    } appWayland;

    static jobject makeFallbackScreen(JNIEnv* env) {
        IRect bounds = IRect::makeXYWH(0, 0, 1920, 1080);
        return classes::Screen::make(env, 1, true, bounds, bounds, 1.f);
    }
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_App__1nStart
        (JNIEnv* env, jclass cls, jobject launcher) {
    jwm::appWayland.init(env);
    jwm::classes::Runnable::run(env, launcher);
    jwm::appWayland.start();
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_App__1nTerminate
        (JNIEnv* env, jclass cls) {
    jwm::appWayland.terminate();
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_io_github_humbleui_jwm_App__1nGetScreens
        (JNIEnv* env, jclass cls) {
    auto screens = jwm::appWayland.getScreens();
    if (screens.empty()) {
        jobjectArray fallback = env->NewObjectArray(1, jwm::classes::Screen::kCls, nullptr);
        jobject screen = jwm::makeFallbackScreen(env);
        env->SetObjectArrayElement(fallback, 0, screen);
        env->DeleteLocalRef(screen);
        return fallback;
    }

    jobjectArray array = env->NewObjectArray(screens.size(), jwm::classes::Screen::kCls, nullptr);
    size_t idx = 0;
    for (const auto& screen : screens) {
        jobject screenObj = screen.asJavaObject(env);
        env->SetObjectArrayElement(array, idx++, screenObj);
        env->DeleteLocalRef(screenObj);
    }
    return array;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_App__1nRunOnUIThread
        (JNIEnv* env, jclass cls, jobject callback) {
    jobject callbackRef = env->NewGlobalRef(callback);
    jwm::appWayland.enqueueCallback(callbackRef);
}

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) {
    return JNI_VERSION_1_2;
}
