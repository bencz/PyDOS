"""Vertical scroll indicator.

Most widgets embed the static ``draw_into`` helper instead of
instantiating a widget: ListView and TextArea paint their own track.
As a widget it also accepts clicks: arrows move one line, the track
jumps a page (no thumb dragging).
"""

from pydos.tui.geometry import Rect
from pydos.tui.color import Style
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Marker, Shade
from pydos.tui.events import EventType


class ScrollBar(Widget):
    def __init__(self, on_scroll=None) -> None:
        super().__init__()
        self.total = 0
        self.window = 0
        self.position = 0
        self.on_scroll = on_scroll

    def set_range(self, total: int, window: int, position: int) -> None:
        self.total = total
        self.window = window
        self.position = position
        self.invalidate()

    @staticmethod
    def thumb_row(rect: Rect, total: int, window: int,
                  position: int) -> int:
        """Track row (absolute y) of the thumb; -1 when nothing scrolls."""
        track: int = rect.height - 2
        if track < 1 or total <= window:
            return -1
        span: int = total - window
        row: int = position * (track - 1) // span if span > 0 else 0
        return rect.y + 1 + row

    @staticmethod
    def draw_into(buffer: Buffer, rect: Rect, total: int, window: int,
                  position: int, style: Style,
                  glyphs: str = "") -> None:
        """``glyphs`` is "<up><down><track><thumb>"; CP437 arrows and
        shades by default, ASCII (e.g. "^v.#") for golden tests."""
        if len(glyphs) < 4:
            glyphs = (Marker.ARROW_UP + Marker.ARROW_DOWN
                      + Shade.LIGHT + Shade.FULL)
        if rect.height < 3 or rect.width < 1:
            return
        buffer.text(rect.x, rect.y, glyphs[0], style)
        buffer.text(rect.x, rect.bottom() - 1, glyphs[1], style)
        buffer.vline(rect.x, rect.y + 1, rect.height - 2,
                     glyphs[2], style)
        thumb: int = ScrollBar.thumb_row(rect, total, window, position)
        if thumb >= 0:
            buffer.text(rect.x, thumb, glyphs[3], style)

    def on_mouse(self, event) -> bool:
        if event.kind != EventType.MOUSE_UP or self.on_scroll is None:
            return event.kind == EventType.MOUSE_DOWN
        if event.y == self.rect.y:
            self.on_scroll(-1)
        elif event.y == self.rect.bottom() - 1:
            self.on_scroll(1)
        else:
            thumb: int = ScrollBar.thumb_row(self.rect, self.total,
                                             self.window, self.position)
            if thumb >= 0 and event.y < thumb:
                self.on_scroll(-self.window)
            elif thumb >= 0 and event.y > thumb:
                self.on_scroll(self.window)
        return True

    def render_self(self, buffer: Buffer) -> None:
        ScrollBar.draw_into(buffer, self.rect, self.total, self.window,
                            self.position, self.theme_style("scrollbar"))
