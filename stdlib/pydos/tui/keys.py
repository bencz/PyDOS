"""Decoded keyboard events and declarative key specs.

``decode_key`` turns the packed integer from the C primitive
``_pydos_tui_key_event`` (ascii | scan << 8 | shift << 16 | ctrl << 17 |
alt << 18) into a ``Key``.  A ``Key`` compares against three things:

    key == Key.F3          # int: legacy code, modifiers ignored
    key == "ctrl+s"        # str: name plus exact modifier match
    key == other_key       # Key: field equality

``code`` keeps the legacy convention: the ASCII value, or 256 + scancode
for special keys, so numeric comparisons stay stable across the old and
new keyboard paths.

Key is a plain class, not a dataclass: it owns __eq__ (the whole point
of the type) and __repr__, and gains nothing from generation.
"""

_SCAN_NAMES: dict = {
    71: "home",
    72: "up",
    73: "page_up",
    75: "left",
    77: "right",
    79: "end",
    80: "down",
    81: "page_down",
    82: "insert",
    83: "delete",
    59: "f1",
    60: "f2",
    61: "f3",
    62: "f4",
    63: "f5",
    64: "f6",
    65: "f7",
    66: "f8",
    67: "f9",
    68: "f10",
    133: "f11",
    134: "f12",
}

_ALT_LETTERS: dict = {
    30: "a", 48: "b", 46: "c", 32: "d", 18: "e", 33: "f", 34: "g",
    35: "h", 23: "i", 36: "j", 37: "k", 38: "l", 50: "m", 49: "n",
    24: "o", 25: "p", 16: "q", 19: "r", 31: "s", 20: "t", 22: "u",
    47: "v", 17: "w", 45: "x", 21: "y", 44: "z",
}

_ASCII_NAMES: dict = {
    8: "backspace",
    9: "tab",
    13: "enter",
    27: "escape",
    32: "space",
}

_NAME_CODES: dict = {
    "escape": 27,
    "enter": 13,
    "tab": 9,
    "backspace": 8,
    "space": 32,
    "home": 327,
    "up": 328,
    "page_up": 329,
    "left": 331,
    "right": 333,
    "end": 335,
    "down": 336,
    "page_down": 337,
    "insert": 338,
    "delete": 339,
    "f1": 315,
    "f2": 316,
    "f3": 317,
    "f4": 318,
    "f5": 319,
    "f6": 320,
    "f7": 321,
    "f8": 322,
    "f9": 323,
    "f10": 324,
    "f11": 389,
    "f12": 390,
}

_SPEC_ALIASES: dict = {
    "esc": "escape",
    "return": "enter",
    "pgup": "page_up",
    "pgdn": "page_down",
    "ins": "insert",
    "del": "delete",
}


def _parse_spec(spec: str) -> tuple:
    """"ctrl+alt+name" -> (code, name, ctrl, alt, shift).

    Sits before Key because names resolve in definition order: Key.__eq__
    consumes this, key_from_spec below wraps it into a Key.
    """
    ctrl: bool = False
    alt: bool = False
    shift: bool = False
    parts: list = spec.lower().split("+")
    name: str = parts[len(parts) - 1]
    i: int = 0
    while i < len(parts) - 1:
        part: str = parts[i]
        if part == "ctrl":
            ctrl = True
        elif part == "alt":
            alt = True
        elif part == "shift":
            shift = True
        i += 1
    if name in _SPEC_ALIASES:
        name = _SPEC_ALIASES[name]

    code: int = 0
    if name in _NAME_CODES:
        code = _NAME_CODES[name]
    elif len(name) == 1:
        if ctrl and "a" <= name <= "z":
            code = ord(name) - 96
        else:
            code = ord(name)
    return (code, name, ctrl, alt, shift)


class Key:
    ESCAPE = 27
    ENTER = 13
    TAB = 9
    BACKSPACE = 8
    SPACE = 32
    HOME = 327
    UP = 328
    PAGE_UP = 329
    LEFT = 331
    RIGHT = 333
    END = 335
    DOWN = 336
    PAGE_DOWN = 337
    INSERT = 338
    DELETE = 339
    F1 = 315
    F2 = 316
    F3 = 317
    F4 = 318
    F5 = 319
    F6 = 320
    F7 = 321
    F8 = 322
    F9 = 323
    F10 = 324
    F11 = 389
    F12 = 390

    def __init__(self, code: int = 0, name: str = "",
                 ctrl: bool = False, alt: bool = False,
                 shift: bool = False) -> None:
        self.code = code
        self.name = name
        self.ctrl = ctrl
        self.alt = alt
        self.shift = shift

    def __repr__(self) -> str:
        return (
            f"Key(code={self.code}, name={self.name!r}, "
            f"ctrl={self.ctrl}, alt={self.alt}, shift={self.shift})"
        )

    def __eq__(self, other) -> bool:
        if isinstance(other, Key):
            return (
                self.code == other.code
                and self.name == other.name
                and self.ctrl == other.ctrl
                and self.alt == other.alt
                and self.shift == other.shift
            )
        if isinstance(other, str):
            spec: tuple = _parse_spec(other)
            return (
                self.name == spec[1]
                and self.ctrl == spec[2]
                and self.alt == spec[3]
                and self.shift == spec[4]
            )
        if isinstance(other, int):
            return self.code == other
        return NotImplemented

    def __ne__(self, other) -> bool:
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def is_printable(self) -> bool:
        """True for a plain text character (what a text field inserts)."""
        return (
            len(self.name) == 1
            and 32 <= self.code <= 126
            and not self.ctrl
            and not self.alt
        )


def decode_key(packed: int) -> Key:
    """Build a Key from the packed _pydos_tui_key_event() integer."""
    ascii_code: int = packed & 255
    scan: int = (packed >> 8) & 255
    shift: bool = (packed & 65536) != 0
    ctrl: bool = (packed & 131072) != 0
    alt: bool = (packed & 262144) != 0
    code: int = ascii_code
    name: str = ""

    if ascii_code == 0:
        code = 256 + scan
        if scan in _SCAN_NAMES:
            name = _SCAN_NAMES[scan]
        elif scan in _ALT_LETTERS:
            name = _ALT_LETTERS[scan]
    elif ascii_code in _ASCII_NAMES:
        name = _ASCII_NAMES[ascii_code]
    elif ctrl and 1 <= ascii_code <= 26:
        name = chr(96 + ascii_code)
    elif 33 <= ascii_code <= 126:
        # Letters normalize to lowercase; case travels in ``shift``.
        if 65 <= ascii_code <= 90:
            name = chr(ascii_code + 32)
        else:
            name = chr(ascii_code)

    return Key(code, name, ctrl, alt, shift)


def key_from_spec(spec: str) -> Key:
    """Parse "ctrl+alt+name" into a Key (used by bindings and tests)."""
    parts: tuple = _parse_spec(spec)
    return Key(parts[0], parts[1], parts[2], parts[3], parts[4])
