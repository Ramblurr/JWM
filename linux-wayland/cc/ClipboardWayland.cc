#include <jni.h>

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_Clipboard__1nSet
        (JNIEnv* env, jclass cls, jobjectArray entries) {
    (void) env;
    (void) cls;
    (void) entries;
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_humbleui_jwm_Clipboard__1nGet
        (JNIEnv* env, jclass cls, jobjectArray formats) {
    (void) cls;
    (void) formats;
    return nullptr;
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_io_github_humbleui_jwm_Clipboard__1nGetFormats
        (JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
    return nullptr;
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_Clipboard__1nClear
        (JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_humbleui_jwm_Clipboard__1nRegisterFormat
        (JNIEnv* env, jclass cls, jstring formatId) {
    (void) env;
    (void) cls;
    (void) formatId;
    return JNI_TRUE;
}
