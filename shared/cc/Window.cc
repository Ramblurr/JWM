#include <iostream>
#include <jni.h>
#include "Window.hh"
#include "impl/JNILocal.hh"
#include "impl/Library.hh"

jwm::Window::~Window() {
    if (fWindow == nullptr) {
        return;
    }

    JNIEnv* env = fEnv;
    bool didAttach = false;
    if (fJvm != nullptr) {
        JNIEnv* currentEnv = nullptr;
        jint status = fJvm->GetEnv(reinterpret_cast<void**>(&currentEnv), JNI_VERSION_1_2);
        if (status == JNI_OK) {
            env = currentEnv;
        } else if (status == JNI_EDETACHED) {
            if (fJvm->AttachCurrentThread(reinterpret_cast<void**>(&currentEnv), nullptr) == JNI_OK) {
                env = currentEnv;
                didAttach = true;
            }
        }
    }

    if (env != nullptr) {
        env->DeleteGlobalRef(fWindow);
    }
    fWindow = nullptr;

    if (didAttach && fJvm != nullptr) {
        fJvm->DetachCurrentThread();
    }
}

void jwm::Window::dispatch(jobject event) {
    if(fWindow)
        jwm::classes::Consumer::accept(fEnv, fWindow, event);
}

jobject jwm::Window::getTextInputClient() const {
    return fEnv->GetObjectField(fWindow, jwm::classes::Window::kTextInputClient);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_Window__1nInit
  (JNIEnv* env, jobject obj) {
    jwm::Window* instance = reinterpret_cast<jwm::Window*>(jwm::classes::Native::fromJava(env, obj));
    instance->fWindow = env->NewGlobalRef(obj);
}
