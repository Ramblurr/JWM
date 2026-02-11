#pragma once

#include <cstdint>

#include "Key.hh"
#include "KeyLocation.hh"

namespace jwm {
    namespace KeyWayland {
        jwm::Key fromKeysym(uint32_t keysym, jwm::KeyLocation& location, int& modifiers);
        bool getKeyState(jwm::Key key);
        void setKeyState(jwm::Key key, bool isDown);
        void clearKeyStates();
        int getModifiers();
    }
}
