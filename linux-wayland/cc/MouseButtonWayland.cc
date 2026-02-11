#include "MouseButtonWayland.hh"

#include <linux/input-event-codes.h>

jwm::MouseButton jwm::MouseButtonWayland::fromNative(uint32_t buttonCode) {
    switch (buttonCode) {
        case BTN_LEFT: return jwm::MouseButton::PRIMARY;
        case BTN_MIDDLE: return jwm::MouseButton::MIDDLE;
        case BTN_RIGHT: return jwm::MouseButton::SECONDARY;
        case BTN_SIDE: return jwm::MouseButton::BACK;
        case BTN_EXTRA: return jwm::MouseButton::FORWARD;
        default: return jwm::MouseButton::PRIMARY;
    }
}

bool jwm::MouseButtonWayland::isButton(uint32_t buttonCode) {
    return buttonCode == BTN_LEFT ||
        buttonCode == BTN_MIDDLE ||
        buttonCode == BTN_RIGHT ||
        buttonCode == BTN_SIDE ||
        buttonCode == BTN_EXTRA;
}

int jwm::MouseButtonWayland::maskForButton(uint32_t buttonCode) {
    if (!isButton(buttonCode)) {
        return 0;
    }
    return static_cast<int>(fromNative(buttonCode));
}
