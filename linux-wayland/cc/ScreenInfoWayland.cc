#include "ScreenInfoWayland.hh"

jobject jwm::ScreenInfoWayland::asJavaObject(JNIEnv* env) const {
    return jwm::classes::Screen::make(env, id, isPrimary, bounds, bounds, scale);
}
