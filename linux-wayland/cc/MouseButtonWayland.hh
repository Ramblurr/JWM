#pragma once

#include <cstdint>

#include "MouseButton.hh"

namespace jwm {
    namespace MouseButtonWayland {
        jwm::MouseButton fromNative(uint32_t buttonCode);
        bool isButton(uint32_t buttonCode);
        int maskForButton(uint32_t buttonCode);
    }
}
