"""Text-mode colors and cell styles.

``Style`` is an immutable-by-convention value that knows how to convert
itself into the VGA text attribute byte: foreground in the low nibble,
background in bits 4-6, bit 7 either blink or bright background
depending on the screen's blink setting.
"""

from dataclasses import dataclass


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


@dataclass
class Style:
    fg: int = 7
    bg: int = 0
    blink: bool = False

    def attr(self) -> int:
        """VGA attribute byte for this style.

        Bit 7 carries the bright-background bit of ``bg`` (8-15) or the
        blink flag — the meaning is decided by the screen's blink mode,
        which Screen.__enter__ switches to bright backgrounds.
        """
        value: int = (self.fg & 15) | ((self.bg & 15) << 4)
        if self.blink:
            value = value | 128
        return value

    def inverted(self) -> "Style":
        return Style(self.bg & 15, self.fg & 15, self.blink)

    def with_fg(self, fg: int) -> "Style":
        return Style(fg, self.bg, self.blink)

    def with_bg(self, bg: int) -> "Style":
        return Style(self.fg, bg, self.blink)
