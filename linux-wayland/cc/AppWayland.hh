#pragma once

#include <jni.h>

#include "WindowManagerWayland.hh"

namespace jwm {
    WindowManagerWayland& getWaylandWindowManager();
    JNIEnv* getWaylandJniEnv();
}
