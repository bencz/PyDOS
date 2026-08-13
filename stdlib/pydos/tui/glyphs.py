"""Box-drawing and marker glyph sets.

Border strings hold six glyphs in a fixed order: top-left, top-right,
bottom-left, bottom-right, horizontal, vertical.  The CP437 bytes are
built with chr() so the source files stay pure ASCII — CP437 literals
would not survive the CPython golden-test harness.

``Border.ASCII`` exists for golden tests: CP437 bytes are translated by
terminals and re-encoded by CPython, so printable expected files must
stick to ASCII.
"""


class Border:
    SINGLE: str = chr(218) + chr(191) + chr(192) + chr(217) + chr(196) + chr(179)
    DOUBLE: str = chr(201) + chr(187) + chr(200) + chr(188) + chr(205) + chr(186)
    HEAVY: str = chr(219) + chr(219) + chr(219) + chr(219) + chr(219) + chr(219)
    ASCII: str = "++++-|"


class Shade:
    LIGHT: str = chr(176)
    MEDIUM: str = chr(177)
    DARK: str = chr(178)
    FULL: str = chr(219)
    ASCII_LIGHT: str = "."
    ASCII_FULL: str = "#"


class Marker:
    CHECK: str = chr(251)
    ARROW_UP: str = chr(24)
    ARROW_DOWN: str = chr(25)
    ARROW_RIGHT: str = chr(26)
    ARROW_LEFT: str = chr(27)
    ASCII_CHECK: str = "x"
    ASCII_ARROW_UP: str = "^"
    ASCII_ARROW_DOWN: str = "v"
    ASCII_ARROW_RIGHT: str = ">"
    ASCII_ARROW_LEFT: str = "<"
