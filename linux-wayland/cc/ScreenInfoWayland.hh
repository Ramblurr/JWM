#pragma once

#include <impl/Library.hh>

namespace jwm {
    struct ScreenInfoWayland {
        long id;
        IRect bounds;
        bool isPrimary;
        float scale;

        jobject asJavaObject(JNIEnv* env) const;
    };
}
