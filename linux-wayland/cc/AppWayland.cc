#include <jni.h>
#include <condition_variable>
#include <mutex>
#include <queue>

#include "impl/Library.hh"

namespace jwm {
    class AppWayland {
    public:
        void init(JNIEnv* env) {
            _jniEnv = env;
            _terminateRequested = false;
        }

        void start() {
            std::unique_lock<std::mutex> lock(_mutex);
            while (!_terminateRequested) {
                _cv.wait(lock, [this] {
                    return _terminateRequested || !_callbacks.empty();
                });

                while (!_callbacks.empty()) {
                    jobject callbackRef = _callbacks.front();
                    _callbacks.pop();
                    lock.unlock();
                    jwm::classes::Runnable::run(_jniEnv, callbackRef);
                    _jniEnv->DeleteGlobalRef(callbackRef);
                    lock.lock();
                }
            }
        }

        void terminate() {
            std::lock_guard<std::mutex> lock(_mutex);
            _terminateRequested = true;
            _cv.notify_all();
        }

        void enqueueCallback(jobject callbackRef) {
            std::lock_guard<std::mutex> lock(_mutex);
            _callbacks.push(callbackRef);
            _cv.notify_all();
        }

    private:
        JNIEnv* _jniEnv = nullptr;
        std::mutex _mutex;
        std::condition_variable _cv;
        std::queue<jobject> _callbacks;
        bool _terminateRequested = false;
    } appWayland;
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
    jobjectArray array = env->NewObjectArray(1, jwm::classes::Screen::kCls, nullptr);
    jwm::IRect bounds = jwm::IRect::makeXYWH(0, 0, 1920, 1080);
    jobject screen = jwm::classes::Screen::make(env, 1, true, bounds, bounds, 1.f);
    env->SetObjectArrayElement(array, 0, screen);
    env->DeleteLocalRef(screen);
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
