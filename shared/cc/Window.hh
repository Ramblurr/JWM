#pragma once
#include <jni.h>
#include "impl/Library.hh"
#include "impl/RefCounted.hh"

namespace jwm {
    class Window: public RefCounted {
    public:
        Window(JNIEnv* env): fEnv(env) {
            if (env != nullptr) {
                env->GetJavaVM(&fJvm);
            }
        }

        virtual ~Window();

        void dispatch(jobject event);

        jobject getTextInputClient() const;

        JNIEnv* fEnv = nullptr;
        JavaVM* fJvm = nullptr;
        jobject fWindow = nullptr;
    };
}
