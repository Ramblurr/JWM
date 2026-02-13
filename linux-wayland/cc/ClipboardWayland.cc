#include <jni.h>

#include <map>
#include <string>
#include <vector>

#include "AppWayland.hh"
#include "StringUTF16.hh"
#include "impl/JNILocal.hh"
#include "impl/Library.hh"

namespace jwm {
    class ClipboardWayland {
    public:
        static ClipboardWayland& inst() {
            static ClipboardWayland s;
            return s;
        }

        void set(JNIEnv* env, jobjectArray entries) {
            if (entries == nullptr) {
                getWaylandWindowManager().clearClipboardContents();
                return;
            }

            std::map<std::string, ByteBuf> contents;
            jsize count = env->GetArrayLength(entries);
            for (jsize i = 0; i < count; ++i) {
                JNILocal<jobject> entry(env, env->GetObjectArrayElement(entries, i));
                if (entry.get() == nullptr) {
                    continue;
                }

                JNILocal<jobject> format(env, classes::ClipboardEntry::getFormat(env, entry.get()));
                JNILocal<jbyteArray> data(env, classes::ClipboardEntry::getData(env, entry.get()));
                if (format.get() == nullptr || data.get() == nullptr) {
                    continue;
                }

                StringUTF16 formatId = StringUTF16::makeFromJString(env, classes::ClipboardFormat::getFormatId(env, format.get()));
                std::string formatKey = formatId.toAscii();
                if (formatKey.empty()) {
                    continue;
                }

                ByteBuf bytes;
                jsize dataSize = env->GetArrayLength(data.get());
                if (dataSize > 0) {
                    jbyte* dataBytes = env->GetByteArrayElements(data.get(), nullptr);
                    if (dataBytes != nullptr) {
                        bytes.insert(bytes.end(), dataBytes, dataBytes + dataSize);
                        env->ReleaseByteArrayElements(data.get(), dataBytes, JNI_ABORT);
                    }
                }

                contents[std::move(formatKey)] = std::move(bytes);
            }

            if (contents.empty()) {
                getWaylandWindowManager().clearClipboardContents();
            } else {
                getWaylandWindowManager().setClipboardContents(std::move(contents));
            }
        }

        jobject get(JNIEnv* env, jobjectArray formats) {
            if (formats == nullptr) {
                return nullptr;
            }

            WindowManagerWayland& manager = getWaylandWindowManager();
            jsize count = env->GetArrayLength(formats);
            for (jsize i = 0; i < count; ++i) {
                JNILocal<jobject> format(env, env->GetObjectArrayElement(formats, i));
                if (format.get() == nullptr) {
                    continue;
                }

                StringUTF16 formatId = StringUTF16::makeFromJString(env, classes::ClipboardFormat::getFormatId(env, format.get()));
                ByteBuf contents;
                if (!manager.getClipboardContents(formatId.toAscii(), contents)) {
                    continue;
                }

                JNILocal<jbyteArray> data(env, env->NewByteArray(static_cast<jsize>(contents.size())));
                if (data.get() == nullptr) {
                    return nullptr;
                }

                env->SetByteArrayRegion(
                    data.get(),
                    0,
                    static_cast<jsize>(contents.size()),
                    reinterpret_cast<const jbyte*>(contents.data()));

                return classes::ClipboardEntry::make(env, format.get(), data.get());
            }

            classes::Throwable::exceptionThrown(env);
            return nullptr;
        }

        jobjectArray getFormats(JNIEnv* env) const {
            std::vector<std::string> formats = getWaylandWindowManager().getClipboardFormats();
            if (formats.empty()) {
                return nullptr;
            }

            jobjectArray jniFormats = env->NewObjectArray(
                static_cast<jsize>(formats.size()),
                classes::ClipboardFormat::kCls,
                nullptr);
            if (jniFormats == nullptr) {
                return nullptr;
            }

            for (jsize i = 0; i < static_cast<jsize>(formats.size()); ++i) {
                JNILocal<jstring> formatId = StringUTF16(formats[i].c_str()).toJString(env);
                jobject format = classes::Clipboard::registerFormat(env, formatId.get());
                if (format == nullptr) {
                    return nullptr;
                }
                env->SetObjectArrayElement(jniFormats, i, format);
            }

            return jniFormats;
        }

        void clear() {
            getWaylandWindowManager().clearClipboardContents();
        }
    };
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_Clipboard__1nSet
        (JNIEnv* env, jclass cls, jobjectArray entries) {
    (void) cls;
    jwm::ClipboardWayland::inst().set(env, entries);
}

extern "C" JNIEXPORT jobject JNICALL Java_io_github_humbleui_jwm_Clipboard__1nGet
        (JNIEnv* env, jclass cls, jobjectArray formats) {
    (void) cls;
    return jwm::ClipboardWayland::inst().get(env, formats);
}

extern "C" JNIEXPORT jobjectArray JNICALL Java_io_github_humbleui_jwm_Clipboard__1nGetFormats
        (JNIEnv* env, jclass cls) {
    (void) cls;
    return jwm::ClipboardWayland::inst().getFormats(env);
}

extern "C" JNIEXPORT void JNICALL Java_io_github_humbleui_jwm_Clipboard__1nClear
        (JNIEnv* env, jclass cls) {
    (void) env;
    (void) cls;
    jwm::ClipboardWayland::inst().clear();
}

extern "C" JNIEXPORT jboolean JNICALL Java_io_github_humbleui_jwm_Clipboard__1nRegisterFormat
        (JNIEnv* env, jclass cls, jstring formatId) {
    (void) env;
    (void) cls;
    (void) formatId;
    return JNI_TRUE;
}
