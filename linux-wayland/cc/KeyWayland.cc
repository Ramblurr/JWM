#include "KeyWayland.hh"

#include <cstddef>

#include <xkbcommon/xkbcommon-keysyms.h>

#include "KeyModifier.hh"

namespace {
    bool gKeyStates[(std::size_t) jwm::Key::_KEY_COUNT] = {0};
}

bool jwm::KeyWayland::getKeyState(jwm::Key key) {
    return gKeyStates[(std::size_t) key];
}

void jwm::KeyWayland::setKeyState(jwm::Key key, bool isDown) {
    if (key == jwm::Key::UNDEFINED) {
        return;
    }
    gKeyStates[(std::size_t) key] = isDown;
}

void jwm::KeyWayland::clearKeyStates() {
    for (std::size_t i = 0; i < (std::size_t) jwm::Key::_KEY_COUNT; ++i) {
        gKeyStates[i] = false;
    }
}

int jwm::KeyWayland::getModifiers() {
    int m = 0;
    if (getKeyState(jwm::Key::CAPS_LOCK  )) m |= (int) jwm::KeyModifier::CAPS_LOCK;
    if (getKeyState(jwm::Key::SHIFT      )) m |= (int) jwm::KeyModifier::SHIFT;
    if (getKeyState(jwm::Key::CONTROL    )) m |= (int) jwm::KeyModifier::CONTROL;
    if (getKeyState(jwm::Key::ALT        )) m |= (int) jwm::KeyModifier::ALT;
    if (getKeyState(jwm::Key::WIN_LOGO   )) m |= (int) jwm::KeyModifier::WIN_LOGO;
    if (getKeyState(jwm::Key::LINUX_META )) m |= (int) jwm::KeyModifier::LINUX_META;
    if (getKeyState(jwm::Key::LINUX_SUPER)) m |= (int) jwm::KeyModifier::LINUX_SUPER;
    return m;
}

jwm::Key jwm::KeyWayland::fromKeysym(uint32_t v, jwm::KeyLocation& location, int& modifiers) {
    location = jwm::KeyLocation::DEFAULT;
    modifiers = 0;

    switch (v) {
        case XKB_KEY_Caps_Lock: return jwm::Key::CAPS_LOCK;
        case XKB_KEY_Shift_R: location = jwm::KeyLocation::RIGHT; // fallthrough
        case XKB_KEY_Shift_L: return jwm::Key::SHIFT;
        case XKB_KEY_Control_R: location = jwm::KeyLocation::RIGHT; // fallthrough
        case XKB_KEY_Control_L: return jwm::Key::CONTROL;
        case XKB_KEY_Alt_R: location = jwm::KeyLocation::RIGHT; // fallthrough
        case XKB_KEY_Alt_L: return jwm::Key::ALT;
        case XKB_KEY_Super_R: location = jwm::KeyLocation::RIGHT; // fallthrough
        case XKB_KEY_Super_L: return jwm::Key::LINUX_SUPER;
        case XKB_KEY_Meta_R: location = jwm::KeyLocation::RIGHT; // fallthrough
        case XKB_KEY_Meta_L: return jwm::Key::LINUX_META;

        case XKB_KEY_Return: return jwm::Key::ENTER;
        case XKB_KEY_BackSpace: return jwm::Key::BACKSPACE;
        case XKB_KEY_Tab: return jwm::Key::TAB;
        case XKB_KEY_Cancel: return jwm::Key::CANCEL;
        case XKB_KEY_Clear: return jwm::Key::CLEAR;
        case XKB_KEY_Pause: return jwm::Key::PAUSE;
        case XKB_KEY_Escape: return jwm::Key::ESCAPE;
        case XKB_KEY_space: return jwm::Key::SPACE;
        case XKB_KEY_Page_Up: return jwm::Key::PAGE_UP;
        case XKB_KEY_Page_Down: return jwm::Key::PAGE_DOWN;
        case XKB_KEY_End: return jwm::Key::END;
        case XKB_KEY_Home: return jwm::Key::HOME;
        case XKB_KEY_Left: return jwm::Key::LEFT;
        case XKB_KEY_Up: return jwm::Key::UP;
        case XKB_KEY_Right: return jwm::Key::RIGHT;
        case XKB_KEY_Down: return jwm::Key::DOWN;
        case XKB_KEY_comma: return jwm::Key::COMMA;
        case XKB_KEY_minus: return jwm::Key::MINUS;
        case XKB_KEY_period: return jwm::Key::PERIOD;
        case XKB_KEY_slash: return jwm::Key::SLASH;
        case XKB_KEY_0: return jwm::Key::DIGIT0;
        case XKB_KEY_1: return jwm::Key::DIGIT1;
        case XKB_KEY_2: return jwm::Key::DIGIT2;
        case XKB_KEY_3: return jwm::Key::DIGIT3;
        case XKB_KEY_4: return jwm::Key::DIGIT4;
        case XKB_KEY_5: return jwm::Key::DIGIT5;
        case XKB_KEY_6: return jwm::Key::DIGIT6;
        case XKB_KEY_7: return jwm::Key::DIGIT7;
        case XKB_KEY_8: return jwm::Key::DIGIT8;
        case XKB_KEY_9: return jwm::Key::DIGIT9;
        case XKB_KEY_semicolon: return jwm::Key::SEMICOLON;
        case XKB_KEY_equal: return jwm::Key::EQUALS;
        case XKB_KEY_a: return jwm::Key::A;
        case XKB_KEY_b: return jwm::Key::B;
        case XKB_KEY_c: return jwm::Key::C;
        case XKB_KEY_d: return jwm::Key::D;
        case XKB_KEY_e: return jwm::Key::E;
        case XKB_KEY_f: return jwm::Key::F;
        case XKB_KEY_g: return jwm::Key::G;
        case XKB_KEY_h: return jwm::Key::H;
        case XKB_KEY_i: return jwm::Key::I;
        case XKB_KEY_j: return jwm::Key::J;
        case XKB_KEY_k: return jwm::Key::K;
        case XKB_KEY_l: return jwm::Key::L;
        case XKB_KEY_m: return jwm::Key::M;
        case XKB_KEY_n: return jwm::Key::N;
        case XKB_KEY_o: return jwm::Key::O;
        case XKB_KEY_p: return jwm::Key::P;
        case XKB_KEY_q: return jwm::Key::Q;
        case XKB_KEY_r: return jwm::Key::R;
        case XKB_KEY_s: return jwm::Key::S;
        case XKB_KEY_t: return jwm::Key::T;
        case XKB_KEY_u: return jwm::Key::U;
        case XKB_KEY_v: return jwm::Key::V;
        case XKB_KEY_w: return jwm::Key::W;
        case XKB_KEY_x: return jwm::Key::X;
        case XKB_KEY_y: return jwm::Key::Y;
        case XKB_KEY_z: return jwm::Key::Z;
        case XKB_KEY_bracketleft: return jwm::Key::OPEN_BRACKET;
        case XKB_KEY_backslash: return jwm::Key::BACK_SLASH;
        case XKB_KEY_bracketright: return jwm::Key::CLOSE_BRACKET;
        case XKB_KEY_KP_0: case XKB_KEY_KP_Insert: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT0;
        case XKB_KEY_KP_1: case XKB_KEY_KP_End: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT1;
        case XKB_KEY_KP_2: case XKB_KEY_KP_Down: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT2;
        case XKB_KEY_KP_3: case XKB_KEY_KP_Page_Down: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT3;
        case XKB_KEY_KP_4: case XKB_KEY_KP_Left: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT4;
        case XKB_KEY_KP_5: case XKB_KEY_KP_Begin: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT5;
        case XKB_KEY_KP_6: case XKB_KEY_KP_Right: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT6;
        case XKB_KEY_KP_7: case XKB_KEY_KP_Home: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT7;
        case XKB_KEY_KP_8: case XKB_KEY_KP_Up: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT8;
        case XKB_KEY_KP_9: case XKB_KEY_KP_Page_Up: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DIGIT9;
        case XKB_KEY_KP_Add: location = jwm::KeyLocation::KEYPAD; return jwm::Key::ADD;
        case XKB_KEY_KP_Separator: location = jwm::KeyLocation::KEYPAD; return jwm::Key::SEPARATOR;
        case XKB_KEY_KP_Subtract: location = jwm::KeyLocation::KEYPAD; return jwm::Key::MINUS;
        case XKB_KEY_KP_Decimal: location = jwm::KeyLocation::KEYPAD; return jwm::Key::PERIOD;
        case XKB_KEY_KP_Divide: location = jwm::KeyLocation::KEYPAD; return jwm::Key::SLASH;
        case XKB_KEY_KP_Delete: location = jwm::KeyLocation::KEYPAD; return jwm::Key::DEL;
        case XKB_KEY_KP_Enter: location = jwm::KeyLocation::KEYPAD; return jwm::Key::ENTER;
        case XKB_KEY_KP_Multiply: location = jwm::KeyLocation::KEYPAD; return jwm::Key::MULTIPLY;
        case XKB_KEY_multiply: return jwm::Key::MULTIPLY;
        case XKB_KEY_Delete: return jwm::Key::DEL;
        case XKB_KEY_Num_Lock: return jwm::Key::NUM_LOCK;
        case XKB_KEY_Scroll_Lock: return jwm::Key::SCROLL_LOCK;
        case XKB_KEY_F1: return jwm::Key::F1;
        case XKB_KEY_F2: return jwm::Key::F2;
        case XKB_KEY_F3: return jwm::Key::F3;
        case XKB_KEY_F4: return jwm::Key::F4;
        case XKB_KEY_F5: return jwm::Key::F5;
        case XKB_KEY_F6: return jwm::Key::F6;
        case XKB_KEY_F7: return jwm::Key::F7;
        case XKB_KEY_F8: return jwm::Key::F8;
        case XKB_KEY_F9: return jwm::Key::F9;
        case XKB_KEY_F10: return jwm::Key::F10;
        case XKB_KEY_F11: return jwm::Key::F11;
        case XKB_KEY_F12: return jwm::Key::F12;
        case XKB_KEY_F13: return jwm::Key::F13;
        case XKB_KEY_F14: return jwm::Key::F14;
        case XKB_KEY_F15: return jwm::Key::F15;
        case XKB_KEY_F16: return jwm::Key::F16;
        case XKB_KEY_F17: return jwm::Key::F17;
        case XKB_KEY_F18: return jwm::Key::F18;
        case XKB_KEY_F19: return jwm::Key::F19;
        case XKB_KEY_F20: return jwm::Key::F20;
        case XKB_KEY_F21: return jwm::Key::F21;
        case XKB_KEY_F22: return jwm::Key::F22;
        case XKB_KEY_F23: return jwm::Key::F23;
        case XKB_KEY_F24: return jwm::Key::F24;
        case XKB_KEY_Print: return jwm::Key::PRINTSCREEN;
        case XKB_KEY_Insert: return jwm::Key::INSERT;
        case XKB_KEY_Help: return jwm::Key::HELP;
        case XKB_KEY_grave: return jwm::Key::BACK_QUOTE;
        case XKB_KEY_quoteright: return jwm::Key::QUOTE;
        case XKB_KEY_Menu: return jwm::Key::MENU;

        case XKB_KEY_exclam: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT1;
        case XKB_KEY_quotedbl: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::QUOTE;
        case XKB_KEY_numbersign: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT3;
        case XKB_KEY_dollar: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT4;
        case XKB_KEY_percent: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT5;
        case XKB_KEY_ampersand: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT7;
        case XKB_KEY_parenleft: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT9;
        case XKB_KEY_parenright: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT0;
        case XKB_KEY_asterisk: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT8;
        case XKB_KEY_plus: return jwm::Key::ADD;
        case XKB_KEY_colon: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::SEMICOLON;
        case XKB_KEY_less: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::COMMA;
        case XKB_KEY_greater: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::PERIOD;
        case XKB_KEY_question: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::SLASH;
        case XKB_KEY_at: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT2;
        case XKB_KEY_asciicircum: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::DIGIT6;
        case XKB_KEY_underscore: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::MINUS;
        case XKB_KEY_braceleft: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::OPEN_BRACKET;
        case XKB_KEY_bar: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::BACK_SLASH;
        case XKB_KEY_braceright: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::CLOSE_BRACKET;
        case XKB_KEY_asciitilde: modifiers = (int) jwm::KeyModifier::SHIFT; return jwm::Key::BACK_QUOTE;
        case XKB_KEY_A: return jwm::Key::A;
        case XKB_KEY_B: return jwm::Key::B;
        case XKB_KEY_C: return jwm::Key::C;
        case XKB_KEY_D: return jwm::Key::D;
        case XKB_KEY_E: return jwm::Key::E;
        case XKB_KEY_F: return jwm::Key::F;
        case XKB_KEY_G: return jwm::Key::G;
        case XKB_KEY_H: return jwm::Key::H;
        case XKB_KEY_I: return jwm::Key::I;
        case XKB_KEY_J: return jwm::Key::J;
        case XKB_KEY_K: return jwm::Key::K;
        case XKB_KEY_L: return jwm::Key::L;
        case XKB_KEY_M: return jwm::Key::M;
        case XKB_KEY_N: return jwm::Key::N;
        case XKB_KEY_O: return jwm::Key::O;
        case XKB_KEY_P: return jwm::Key::P;
        case XKB_KEY_Q: return jwm::Key::Q;
        case XKB_KEY_R: return jwm::Key::R;
        case XKB_KEY_S: return jwm::Key::S;
        case XKB_KEY_T: return jwm::Key::T;
        case XKB_KEY_U: return jwm::Key::U;
        case XKB_KEY_V: return jwm::Key::V;
        case XKB_KEY_W: return jwm::Key::W;
        case XKB_KEY_X: return jwm::Key::X;
        case XKB_KEY_Y: return jwm::Key::Y;
        case XKB_KEY_Z: return jwm::Key::Z;
        default: return jwm::Key::UNDEFINED;
    }
}
