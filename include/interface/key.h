#pragma once

#include <algorithm>
#include <array>
#include <cctype>
#include <iterator>
#include <utility>

namespace Input
{

//we only include the scancode we need, many or them are ignored, but the value
//here is correct, based on * The values in this enumeration are based on the
// USB usage page standard:
// https://www.usb.org/sites/default/files/documents/hut1_12v2.pdf
enum ScanCode {
    ScanCode_Invalid = 0,
    Scancode_Capslock = 57,

    Scancode_Printscreen = 70,
    Scancode_Scrolllock = 71,
    Scancode_Pause = 72,
    Scancode_Insert = 73,

    Scancode_Home = 74,
    Scancode_Pageup = 75,
    Scancode_Delete = 76,
    Scancode_End = 77,
    Scancode_Pagedown = 78,
    Scancode_Right = 79,
    Scancode_Left = 80,
    Scancode_Down = 81,
    Scancode_Up = 82,

    Scancode_Lctrl = 224,
    Scancode_Lshift = 225,
    Scancode_Lalt = 226, ///< Alt, Option
    Scancode_Lgui = 227, ///< Windows, Command (Apple), Meta
    Scancode_Rctrl = 228,
    Scancode_Rshift = 229,
    Scancode_Ralt = 230, ///< Alt gr, Option
    Scancode_Rgui = 231, ///< Windows, Command (Apple), Meta
};

//Imgui requires us to map all keycode within 512 indices, so we just use 256
//for mask, this unfortuately will map scancode onto latin unicodes
constexpr unsigned
KEYCODE_FROM_SCANCODE(enum ScanCode x) { return (x | 0x100); }

enum Key {
    Key_Unknown = 0,

    Key_Return = '\r',
    Key_Enter = '\n',
    Key_Escape = '\033',
    Key_Backspace = '\b',
    Key_Tab = '\t',
    Key_Delete = '\177',

    /* printable chars */
    Key_Space = ' ',
    Key_Any = Key_Space,
    Key_Exclaim = '!',
    Key_QuoteDbl = '"',
    Key_Hash = '#',
    Key_Percent = '%',
    Key_Dollar = '$',
    Key_Ampersand = '&',
    Key_Quote = '\'',
    Key_Leftparen = '(',
    Key_Rightparen = ')',
    Key_LeftBrace = '{',
    Key_RightBrace = '}',
    Key_Asterisk = '*',
    Key_Plus = '+',
    Key_Comma = ',',
    Key_Minus = '-',
    Key_Period = '.',
    Key_Slash = '/',
    Key_0 = '0',
    Key_1 = '1',
    Key_2 = '2',
    Key_3 = '3',
    Key_4 = '4',
    Key_5 = '5',
    Key_6 = '6',
    Key_7 = '7',
    Key_8 = '8',
    Key_9 = '9',
    Key_Colon = ':',
    Key_Semicolon = ';',
    Key_Less = '<',
    Key_Equals = '=',
    Key_Greater = '>',
    Key_Question = '?',
    Key_At = '@',
    Key_Bar = '|',
    /*
      Skip uppercase letters
    */
    Key_Leftbracket = '[',
    Key_Backslash = '\\',
    Key_Rightbracket = ']',
    Key_Caret = '^',
    Key_Underscore = '_',
    Key_Backquote = '`',
    Key_a = 'a',
    Key_b = 'b',
    Key_c = 'c',
    Key_d = 'd',
    Key_e = 'e',
    Key_f = 'f',
    Key_g = 'g',
    Key_h = 'h',
    Key_i = 'i',
    Key_j = 'j',
    Key_k = 'k',
    Key_l = 'l',
    Key_m = 'm',
    Key_n = 'n',
    Key_o = 'o',
    Key_p = 'p',
    Key_q = 'q',
    Key_r = 'r',
    Key_s = 's',
    Key_t = 't',
    Key_u = 'u',
    Key_v = 'v',
    Key_w = 'w',
    Key_x = 'x',
    Key_y = 'y',
    Key_z = 'z',

    /* modifiers and controls */
    Key_Capslock = KEYCODE_FROM_SCANCODE(Scancode_Capslock),

    Key_Printscreen = KEYCODE_FROM_SCANCODE(Scancode_Printscreen),
    Key_Scrolllock = KEYCODE_FROM_SCANCODE(Scancode_Scrolllock),
    Key_Pause = KEYCODE_FROM_SCANCODE(Scancode_Pause),
    Key_Insert = KEYCODE_FROM_SCANCODE(Scancode_Insert),
    Key_Home = KEYCODE_FROM_SCANCODE(Scancode_Home),
    Key_PageUp = KEYCODE_FROM_SCANCODE(Scancode_Pageup),
    Key_End = KEYCODE_FROM_SCANCODE(Scancode_End),
    Key_PageDown = KEYCODE_FROM_SCANCODE(Scancode_Pagedown),
    Key_Right = KEYCODE_FROM_SCANCODE(Scancode_Right),
    Key_Left = KEYCODE_FROM_SCANCODE(Scancode_Left),
    Key_Down = KEYCODE_FROM_SCANCODE(Scancode_Down),
    Key_Up = KEYCODE_FROM_SCANCODE(Scancode_Up),

    Key_Lctrl = KEYCODE_FROM_SCANCODE(Scancode_Lctrl),
    Key_Lshift = KEYCODE_FROM_SCANCODE(Scancode_Lshift),
    Key_Lalt = KEYCODE_FROM_SCANCODE(Scancode_Lalt),
    Key_Lgui = KEYCODE_FROM_SCANCODE(Scancode_Lgui),
    Key_Rctrl = KEYCODE_FROM_SCANCODE(Scancode_Rctrl),
    Key_Rshift = KEYCODE_FROM_SCANCODE(Scancode_Rshift),
    Key_Ralt = KEYCODE_FROM_SCANCODE(Scancode_Ralt),
    Key_Rgui = KEYCODE_FROM_SCANCODE(Scancode_Rgui),
    //The rest of the keycode are not related to us, not used here
};

enum Modifier {
    Mod_None = 0x0000,
    Mod_Shift = 0x0001,
    Mod_Ctrl = 0x0002,
    Mod_Alt = 0x0004,
    Mod_Meta = 0x0008,
};

inline enum Key getKeyREBTD(unsigned int key) {
    switch (key) {
    case Key_Return:
    case Key_Enter:
    case Key_Escape:
    case Key_Tab:
    case Key_Delete:
        return (Key)key;
    default:
        return Key_Unknown;
    }
}

inline enum Key keyFromScanCode(unsigned scan) {
    typedef struct { enum ScanCode l, r; } region_t;

    region_t regions[] = {
        {Scancode_Capslock, Scancode_Capslock},
        {Scancode_Printscreen, Scancode_Insert},
        {Scancode_Home, Scancode_Up},
        {Scancode_Lctrl, Scancode_Rgui}
    };
    for (auto region: regions) {
        if (scan >= region.l && scan <= region.r)
            return (enum Key)KEYCODE_FROM_SCANCODE((enum ScanCode)scan);
    }
    return Key_Unknown;
}

}
