"""Scrolling selection list.

Enter (or a second click on the selected row) activates the item.  The
scroll indicator appears automatically when the list overflows.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType
from pydos.tui.widgets.scrollbr import ScrollBar


class ListView(Widget):
    def __init__(self, items: list, selected: int = 0, on_select=None,
                 on_activate=None) -> None:
        super().__init__()
        self._items = items
        self._selected = selected
        self.top = 0
        self.on_select = on_select
        self.on_activate = on_activate
        self.scroll_glyphs = ""
        self.focusable = True

    @property
    def items(self) -> list:
        return self._items

    @items.setter
    def items(self, value: list) -> None:
        self._items = value
        self._selected = 0
        self.top = 0
        self.invalidate()

    @property
    def selected(self) -> int:
        return self._selected

    @selected.setter
    def selected(self, value: int) -> None:
        self._selected = value
        self.invalidate()

    def select(self, index: int) -> None:
        if index < 0:
            index = 0
        if index >= len(self._items):
            index = len(self._items) - 1
        if index < 0 or index == self._selected:
            return
        self._selected = index
        self.invalidate()
        if self.on_select is not None:
            self.on_select(index)

    def activate(self) -> None:
        if self.on_activate is not None and self._selected >= 0:
            self.on_activate(self._selected)

    def _ensure_visible(self) -> None:
        height: int = self.rect.height
        if height <= 0:
            return
        if self._selected < self.top:
            self.top = self._selected
        if self._selected >= self.top + height:
            self.top = self._selected - height + 1

    def on_key(self, event) -> bool:
        key = event.key
        if key == Key.UP:
            self.select(self._selected - 1)
            return True
        if key == Key.DOWN:
            self.select(self._selected + 1)
            return True
        if key == Key.HOME:
            self.select(0)
            return True
        if key == Key.END:
            self.select(len(self._items) - 1)
            return True
        if key == Key.PAGE_UP:
            self.select(self._selected - self.rect.height)
            return True
        if key == Key.PAGE_DOWN:
            self.select(self._selected + self.rect.height)
            return True
        if key == Key.ENTER:
            self.activate()
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            row: int = self.top + (event.y - self.rect.y)
            if 0 <= row < len(self._items):
                if row == self._selected:
                    self.activate()
                else:
                    self.select(row)
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        self._ensure_visible()
        overflow: bool = len(self._items) > self.rect.height
        text_width: int = self.rect.width - (1 if overflow else 0)
        base = self.theme_style("list")
        chosen = self.theme_style("list.selected")
        buffer.fill(self.rect, " ", base)
        row: int = 0
        while row < self.rect.height and self.top + row < len(self._items):
            index: int = self.top + row
            shown: str = self._items[index]
            if len(shown) > text_width:
                shown = shown[:text_width]
            else:
                shown = shown + " " * (text_width - len(shown))
            style = chosen if index == self._selected else base
            buffer.text(self.rect.x, self.rect.y + row, shown, style)
            row += 1
        if overflow:
            ScrollBar.draw_into(
                buffer,
                Rect(self.rect.right() - 1, self.rect.y, 1,
                     self.rect.height),
                len(self._items), self.rect.height, self.top,
                self.theme_style("scrollbar"), self.scroll_glyphs,
            )
