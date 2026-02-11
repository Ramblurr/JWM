#include <jni.h>

#include "impl/Library.hh"
#include "impl/RefCounted.hh"

namespace {
    class LayerStub: public jwm::RefCounted {
    };
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerGL__1nMake
        (JNIEnv* env, jclass cls) {
    LayerStub* instance = new LayerStub();
    return reinterpret_cast<jlong>(instance);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nAttach
        (JNIEnv* env, jobject obj, jobject windowObj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nReconfigure
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nMakeCurrent
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nResize
        (JNIEnv* env, jobject obj, jint width, jint height) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nSwapBuffers
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerGL__1nClose
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nMake
        (JNIEnv* env, jclass cls) {
    LayerStub* instance = new LayerStub();
    return reinterpret_cast<jlong>(instance);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nAttach
        (JNIEnv* env, jobject obj, jobject windowObj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nReconfigure
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nResize
        (JNIEnv* env, jobject obj, jint width, jint height) {
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nSwapBuffers
        (JNIEnv* env, jobject obj) {
}

extern "C" JNIEXPORT jlong JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nGetPixelsPtr
        (JNIEnv* env, jobject obj) {
    return 0;
}

extern "C" JNIEXPORT jint JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nGetRowBytes
        (JNIEnv* env, jobject obj) {
    return 0;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_LayerRaster__1nClose
        (JNIEnv* env, jobject obj) {
}
