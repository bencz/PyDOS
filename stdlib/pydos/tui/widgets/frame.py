"""Bordered container with an optional title.

The single child (or all children) get the inset interior.  The title
is a property so an editor can live-update "EDIT  FILE.TXT *" and rely
on invalidation.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Border


class Frame(Widget):
    def __init__(self, child=None, title: str = "",
                 border: str = "") -> None:
        super().__init__()
        self._title = title
        self.border = border if len(border) >= 6 else Border.SINGLE
        if child is not None:
            self.add(child)

    @property
    def title(self) -> str:
        return self._title

    @title.setter
    def title(self, value: str) -> None:
        self._title = value
        self.invalidate()

    def layout(self) -> None:
        interior: Rect = self.rect.inset(1)
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            child.rect = Rect(interior.x, interior.y,
                              interior.width, interior.height)
            child.layout()
            i += 1

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.width < 2 or self.rect.height < 2:
            return
        style = self.theme_style("frame")
        buffer.fill(self.rect, " ", style)
        buffer.box(self.rect, style, "", self.border)
        if len(self._title) > 0 and self.rect.width > 4:
            shown: str = " " + self._title + " "
            if len(shown) > self.rect.width - 2:
                shown = shown[:self.rect.width - 2]
            buffer.text(self.rect.x + 2, self.rect.y, shown,
                        self.theme_style("frame.title"))
