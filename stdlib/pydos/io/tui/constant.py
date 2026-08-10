"""Stable DOS text-mode colors and keyboard codes.

The physical filename is the DOS 8.3 alias for tui.constants.
"""


class Color:
    BLACK = 0
    BLUE = 1
    GREEN = 2
    CYAN = 3
    RED = 4
    MAGENTA = 5
    BROWN = 6
    LIGHT_GRAY = 7
    DARK_GRAY = 8
    LIGHT_BLUE = 9
    LIGHT_GREEN = 10
    LIGHT_CYAN = 11
    LIGHT_RED = 12
    LIGHT_MAGENTA = 13
    YELLOW = 14
    WHITE = 15


class Key:
    ESCAPE = 27
    ENTER = 13
    BACKSPACE = 8
    TAB = 9
    UP = 256 + 72
    DOWN = 256 + 80
    LEFT = 256 + 75
    RIGHT = 256 + 77
    HOME = 256 + 71
    END = 256 + 79
    PAGE_UP = 256 + 73
    PAGE_DOWN = 256 + 81
    DELETE = 256 + 83
    INSERT = 256 + 82
    F1 = 256 + 59
    F2 = 256 + 60
    F3 = 256 + 61
    F4 = 256 + 62
    F5 = 256 + 63
    F6 = 256 + 64
    F7 = 256 + 65
    F8 = 256 + 66
    F9 = 256 + 67
    F10 = 256 + 68

    ALT_E = 256 + 18
    ALT_F = 256 + 33
    ALT_H = 256 + 35
    ALT_S = 256 + 31
    ALT_V = 256 + 47

    CTRL_F = 6
    CTRL_G = 7
    CTRL_N = 14
    CTRL_O = 15
    CTRL_Q = 17
    CTRL_S = 19
