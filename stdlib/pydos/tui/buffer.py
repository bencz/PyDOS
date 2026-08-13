"""In-memory screen composition with a glyph plane and an attribute plane.

Two parallel lists of one string per row, one byte per cell in each:
painting a whole line costs a couple of C string primitives, and the
buffer stays trivially inspectable — ``print(buffer)`` shows the glyph
plane, ``print(buffer.attr_map())`` the attribute plane in hex, which is
what the golden tests compare.

All drawing respects the clip stack::

    with buffer.clip(Rect(1, 1, 20, 5)):
        buffer.text(0, 0, "clipped", style)

Buffers know nothing about the screen; ``Screen.present`` hands the two
planes to the C engine, which writes only the cells that changed.
"""

from pydos.tui.geometry import Rect
from pydos.tui.color import Style
from pydos.tui.glyphs import Border

_HEX_DIGITS: str = "0123456789abcdef"


class _ClipGuard:
    # ``buffer`` is a Buffer; the class is defined later in this module
    # and the type checker resolves names in order, so these two spots
    # stay unannotated.
    def __init__(self, buffer, rect: Rect) -> None:
        self.buffer = buffer
        self.rect = rect

    def __enter__(self):
        self.buffer.push_clip(self.rect)
        return self.buffer

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        self.buffer.pop_clip()
        return False


class Buffer:
    def __init__(self, width: int, height: int) -> None:
        if width < 0:
            width = 0
        if height < 0:
            height = 0
        self.width = width
        self.height = height
        blank_glyphs: str = " " * width
        blank_attrs: str = chr(7) * width
        self.glyph_rows: list = [blank_glyphs for i in range(height)]
        self.attr_rows: list = [blank_attrs for i in range(height)]
        self.clips: list = []

    # -- clip stack ------------------------------------------------- #

    def push_clip(self, rect: Rect) -> None:
        self.clips.append(self.bounds().intersect(rect))

    def pop_clip(self) -> None:
        if len(self.clips) > 0:
            self.clips.pop()

    def clip(self, rect: Rect) -> _ClipGuard:
        return _ClipGuard(self, rect)

    def bounds(self) -> Rect:
        """Active drawing region: the innermost clip, or the whole buffer."""
        if len(self.clips) > 0:
            return self.clips[len(self.clips) - 1]
        return Rect(0, 0, self.width, self.height)

    # -- drawing ---------------------------------------------------- #

    def clear(self, style: Style, fill: str = " ") -> None:
        self.fill(Rect(0, 0, self.width, self.height), fill, style)

    def text(self, x: int, y: int, s: str, style: Style) -> None:
        """Write a run of glyphs with one style, clipped to bounds."""
        region: Rect = self.bounds()
        if y < region.y or y >= region.bottom():
            return
        start: int = 0
        if x < region.x:
            start = region.x - x
            x = region.x
        end: int = len(s)
        if x + (end - start) > region.right():
            end = start + (region.right() - x)
        if start >= end:
            return
        visible: str = s[start:end]
        glyph_row: str = self.glyph_rows[y]
        attr_row: str = self.attr_rows[y]
        attr_run: str = chr(style.attr()) * len(visible)
        self.glyph_rows[y] = (
            glyph_row[:x] + visible + glyph_row[x + len(visible):]
        )
        self.attr_rows[y] = (
            attr_row[:x] + attr_run + attr_row[x + len(visible):]
        )

    def fill(self, rect: Rect, ch: str, style: Style) -> None:
        region: Rect = self.bounds().intersect(rect)
        if region.is_empty():
            return
        run: str = ch * region.width
        attr_run: str = chr(style.attr()) * region.width
        y: int = region.y
        while y < region.bottom():
            glyph_row: str = self.glyph_rows[y]
            attr_row: str = self.attr_rows[y]
            self.glyph_rows[y] = (
                glyph_row[:region.x] + run + glyph_row[region.right():]
            )
            self.attr_rows[y] = (
                attr_row[:region.x] + attr_run + attr_row[region.right():]
            )
            y += 1

    def hline(self, x: int, y: int, length: int, ch: str, style: Style) -> None:
        if length > 0:
            self.text(x, y, ch * length, style)

    def vline(self, x: int, y: int, length: int, ch: str, style: Style) -> None:
        row: int = y
        while row < y + length:
            self.text(x, row, ch, style)
            row += 1

    def box(self, rect: Rect, style: Style, title: str = "",
            border: str = "") -> None:
        """Border (and optional title) along the edge of ``rect``.

        ``border`` is a six-glyph set from pydos.tui.glyphs; the default
        is Border.SINGLE.  The interior is left untouched — fill it
        explicitly when a solid panel is wanted.
        """
        if rect.width < 2 or rect.height < 2:
            return
        if len(border) < 6:
            border = Border.SINGLE
        top: str = border[0] + border[4] * (rect.width - 2) + border[1]
        bottom_row: str = border[2] + border[4] * (rect.width - 2) + border[3]
        self.text(rect.x, rect.y, top, style)
        self.text(rect.x, rect.bottom() - 1, bottom_row, style)
        self.vline(rect.x, rect.y + 1, rect.height - 2, border[5], style)
        self.vline(rect.right() - 1, rect.y + 1, rect.height - 2,
                   border[5], style)
        if len(title) > 0 and rect.width > 4:
            shown: str = " " + title + " "
            if len(shown) > rect.width - 2:
                shown = shown[:rect.width - 2]
            self.text(rect.x + 2, rect.y, shown, style)

    def blit(self, src: "Buffer", x: int, y: int) -> None:
        """Copy another buffer (both planes) at (x, y), clipped."""
        region: Rect = self.bounds().intersect(
            Rect(x, y, src.width, src.height)
        )
        if region.is_empty():
            return
        src_x: int = region.x - x
        row: int = region.y
        while row < region.bottom():
            src_row: int = row - y
            glyph_run: str = src.glyph_rows[src_row][src_x:src_x + region.width]
            attr_run: str = src.attr_rows[src_row][src_x:src_x + region.width]
            glyph_row: str = self.glyph_rows[row]
            attr_row: str = self.attr_rows[row]
            self.glyph_rows[row] = (
                glyph_row[:region.x] + glyph_run + glyph_row[region.right():]
            )
            self.attr_rows[row] = (
                attr_row[:region.x] + attr_run + attr_row[region.right():]
            )
            row += 1

    def shade(self, rect: Rect) -> None:
        """Darken attributes only (dialog drop shadows): glyphs remain."""
        region: Rect = self.bounds().intersect(rect)
        if region.is_empty():
            return
        shadow_run: str = chr(8) * region.width
        y: int = region.y
        while y < region.bottom():
            attr_row: str = self.attr_rows[y]
            self.attr_rows[y] = (
                attr_row[:region.x] + shadow_run + attr_row[region.right():]
            )
            y += 1

    # -- inspection -------------------------------------------------- #

    def cell(self, x: int, y: int) -> str:
        """Glyph at (x, y); empty string outside the buffer."""
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return ""
        return self.glyph_rows[y][x]

    def cell_attr(self, x: int, y: int) -> int:
        """Attribute byte at (x, y); -1 outside the buffer."""
        if x < 0 or y < 0 or x >= self.width or y >= self.height:
            return -1
        return ord(self.attr_rows[y][x])

    def attr_map(self) -> str:
        """Attribute plane as two lowercase hex digits per cell."""
        lines: list = []
        y: int = 0
        while y < self.height:
            attr_row: str = self.attr_rows[y]
            parts: list = []
            x: int = 0
            while x < self.width:
                code: int = ord(attr_row[x])
                parts.append(_HEX_DIGITS[code >> 4] + _HEX_DIGITS[code & 15])
                x += 1
            lines.append("".join(parts))
            y += 1
        return "\n".join(lines)

    def __str__(self) -> str:
        return "\n".join(self.glyph_rows)
