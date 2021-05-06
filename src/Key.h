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
enum ScanCode
{
    SCANCODE_INVALID = 0,
    SCANCODE_CAPSLOCK = 57,

    SCANCODE_PRINTSCREEN = 70,
    SCANCODE_SCROLLLOCK = 71,
    SCANCODE_PAUSE = 72,
    SCANCODE_INSERT = 73,

    SCANCODE_HOME = 74,
    SCANCODE_PAGEUP = 75,
    SCANCODE_DELETE = 76,
    SCANCODE_END = 77,
    SCANCODE_PAGEDOWN = 78,
    SCANCODE_RIGHT = 79,
    SCANCODE_LEFT = 80,
    SCANCODE_DOWN = 81,
    SCANCODE_UP = 82,

    SCANCODE_LCTRL = 224,
    SCANCODE_LSHIFT = 225,
    SCANCODE_LALT = 226, ///< Alt, Option
    SCANCODE_LGUI = 227, ///< Windows, Command (Apple), Meta
    SCANCODE_RCTRL = 228,
    SCANCODE_RSHIFT = 229,
    SCANCODE_RALT = 230, ///< Alt gr, Option
    SCANCODE_RGUI = 231, ///< Windows, Command (Apple), Meta
};

//Imgui requires us to map all keycode within 512 indices, so we just use 256
//for mask, this unfortuately will map scancode onto latin unicodes
constexpr unsigned
KEYCODE_FROM_SCANCODE(enum ScanCode x) { return (x | 0x100); }

enum Key
{
    KEY_UNKNOWN = 0,

    KEY_RETURN = '\r',
    KEY_ENTER = '\n',
    KEY_ESCAPE = '\033',
    KEY_BACKSPACE = '\b',
    KEY_TAB = '\t',
    KEY_DELETE = '\177',

    /* printable chars */
    KEY_SPACE = ' ',
    KEY_ANY = KEY_SPACE,
    KEY_EXCLAIM = '!',
    KEY_QUOTEDBL = '"',
    KEY_HASH = '#',
    KEY_PERCENT = '%',
    KEY_DOLLAR = '$',
    KEY_AMPERSAND = '&',
    KEY_QUOTE = '\'',
    KEY_LEFTPAREN = '(',
    KEY_RIGHTPAREN = ')',
    KEY_LEFTBRACE = '{',
    KEY_RIGHTBRACE = '}',
    KEY_ASTERISK = '*',
    KEY_PLUS = '+',
    KEY_COMMA = ',',
    KEY_MINUS = '-',
    KEY_PERIOD = '.',
    KEY_SLASH = '/',
    KEY_0 = '0',
    KEY_1 = '1',
    KEY_2 = '2',
    KEY_3 = '3',
    KEY_4 = '4',
    KEY_5 = '5',
    KEY_6 = '6',
    KEY_7 = '7',
    KEY_8 = '8',
    KEY_9 = '9',
    KEY_COLON = ':',
    KEY_SEMICOLON = ';',
    KEY_LESS = '<',
    KEY_EQUALS = '=',
    KEY_GREATER = '>',
    KEY_QUESTION = '?',
    KEY_AT = '@',
    KEY_BAR = '|',
    /*
      Skip uppercase letters
    */
    KEY_LEFTBRACKET = '[',
    KEY_BACKSLASH = '\\',
    KEY_RIGHTBRACKET = ']',
    KEY_CARET = '^',
    KEY_UNDERSCORE = '_',
    KEY_BACKQUOTE = '`',
    KEY_A = 'a',
    KEY_B = 'b',
    KEY_C = 'c',
    KEY_D = 'd',
    KEY_E = 'e',
    KEY_F = 'f',
    KEY_G = 'g',
    KEY_H = 'h',
    KEY_I = 'i',
    KEY_J = 'j',
    KEY_K = 'k',
    KEY_L = 'l',
    KEY_M = 'm',
    KEY_N = 'n',
    KEY_O = 'o',
    KEY_P = 'p',
    KEY_Q = 'q',
    KEY_R = 'r',
    KEY_S = 's',
    KEY_T = 't',
    KEY_U = 'u',
    KEY_V = 'v',
    KEY_W = 'w',
    KEY_X = 'x',
    KEY_Y = 'y',
    KEY_Z = 'z',

    /* modifiers and controls */
    KEY_CAPSLOCK = KEYCODE_FROM_SCANCODE(SCANCODE_CAPSLOCK),

    KEY_PRINTSCREEN = KEYCODE_FROM_SCANCODE(SCANCODE_PRINTSCREEN),
    KEY_SCROLLLOCK = KEYCODE_FROM_SCANCODE(SCANCODE_SCROLLLOCK),
    KEY_PAUSE = KEYCODE_FROM_SCANCODE(SCANCODE_PAUSE),
    KEY_INSERT = KEYCODE_FROM_SCANCODE(SCANCODE_INSERT),
    KEY_HOME = KEYCODE_FROM_SCANCODE(SCANCODE_HOME),
    KEY_PAGEUP = KEYCODE_FROM_SCANCODE(SCANCODE_PAGEUP),
    KEY_END = KEYCODE_FROM_SCANCODE(SCANCODE_END),
    KEY_PAGEDOWN = KEYCODE_FROM_SCANCODE(SCANCODE_PAGEDOWN),
    KEY_RIGHT = KEYCODE_FROM_SCANCODE(SCANCODE_RIGHT),
    KEY_LEFT = KEYCODE_FROM_SCANCODE(SCANCODE_LEFT),
    KEY_DOWN = KEYCODE_FROM_SCANCODE(SCANCODE_DOWN),
    KEY_UP = KEYCODE_FROM_SCANCODE(SCANCODE_UP),

    KEY_LCTRL = KEYCODE_FROM_SCANCODE(SCANCODE_LCTRL),
    KEY_LSHIFT = KEYCODE_FROM_SCANCODE(SCANCODE_LSHIFT),
    KEY_LALT = KEYCODE_FROM_SCANCODE(SCANCODE_LALT),
    KEY_LGUI = KEYCODE_FROM_SCANCODE(SCANCODE_LGUI),
    KEY_RCTRL = KEYCODE_FROM_SCANCODE(SCANCODE_RCTRL),
    KEY_RSHIFT = KEYCODE_FROM_SCANCODE(SCANCODE_RSHIFT),
    KEY_RALT = KEYCODE_FROM_SCANCODE(SCANCODE_RALT),
    KEY_RGUI = KEYCODE_FROM_SCANCODE(SCANCODE_RGUI),
    //The rest of the keycode are not related to us, not used here
};

enum Modifier
{
    MOD_NONE = 0x0000,
    MOD_SHIFT = 0x0001,
    MOD_CTRL = 0x0002,
    MOD_ALT = 0x0004,
    MOD_META = 0x0008,
};

inline enum Key getKeyREBTD(unsigned int key)
{
    switch (key)
    {
    case KEY_RETURN:
    case KEY_ENTER:
    case KEY_ESCAPE:
    case KEY_TAB:
    case KEY_DELETE:
        return (Key)key;
    default:
        return KEY_UNKNOWN;
    }
}

inline enum Key keyFromScanCode(unsigned scan)
{
    typedef struct
    {
        enum ScanCode l, r;
    } region_t;

    region_t regions[] =
        {
            {SCANCODE_CAPSLOCK, SCANCODE_CAPSLOCK},
            {SCANCODE_PRINTSCREEN, SCANCODE_INSERT},
            {SCANCODE_HOME, SCANCODE_UP},
            {SCANCODE_LCTRL, SCANCODE_RGUI}
        };
    for (auto region: regions)
    {
        if (scan >= region.l && scan <= region.r)
            return (enum Key)KEYCODE_FROM_SCANCODE((enum ScanCode)scan);
    }
    return KEY_UNKNOWN;
}

}
