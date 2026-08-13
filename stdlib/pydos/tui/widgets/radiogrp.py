"""Exclusive choice list: one focusable widget for the whole group.

Up/Down move the selection directly (classic DOS behavior); a mouse
click selects the row under the pointer.
"""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class RadioGroup(Widget):
    def __init__(self, options: list, selected: int = 0,
                 on_change=None) -> None:
        super().__init__()
        self.options = options
        self._selected = selected
        self.on_change = on_change
        self.focusable = True
        self.size_hint = len(options)

    @property
    def selected(self) -> int:
        return self._selected

    @selected.setter
    def selected(self, value: int) -> None:
        self._selected = value
        self.invalidate()

    def select(self, index: int) -> None:
        if index < 0 or index >= len(self.options):
            return
        if index != self._selected:
            self._selected = index
            self.invalidate()
            if self.on_change is not None:
                self.on_change(index)

    def on_key(self, event) -> bool:
        if event.key == Key.UP:
            self.select(self._selected - 1)
            return True
        if event.key == Key.DOWN:
            self.select(self._selected + 1)
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            self.select(event.y - self.rect.y)
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        i: int = 0
        while i < len(self.options) and i < self.rect.height:
            if i == self._selected and self.focused:
                style = self.theme_style("check.focus")
            else:
                style = self.theme_style("check")
            mark: str = "*" if i == self._selected else " "
            shown: str = "(" + mark + ") " + self.options[i]
            if len(shown) > self.rect.width:
                shown = shown[:self.rect.width]
            buffer.text(self.rect.x, self.rect.y + i, shown, style)
            i += 1
