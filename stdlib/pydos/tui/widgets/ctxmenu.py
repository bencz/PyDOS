"""Context menu: a small modal list of actions at a screen position.

    action = app.run_modal(ContextMenu(
        [("Cut", "cut"), ("Copy", "copy"), ("Paste", "paste")], x, y))

``result`` is the chosen action string, or None when dismissed.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Border
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class ContextMenu(Widget):
    def __init__(self, items: list, x: int, y: int) -> None:
        super().__init__()
        self.items = items
        self.want_x = x
        self.want_y = y
        self.item_index = 0
        self.open_flag = False
        self.result = None
        self.border = Border.SINGLE
        self.focusable = True

    def close(self, result) -> None:
        self.result = result
        self.open_flag = False

    def place(self, screen_width: int, screen_height: int) -> None:
        width: int = 4
        i: int = 0
        while i < len(self.items):
            if len(self.items[i][0]) + 4 > width:
                width = len(self.items[i][0]) + 4
            i += 1
        height: int = len(self.items) + 2
        x: int = self.want_x
        y: int = self.want_y
        if x + width > screen_width:
            x = screen_width - width
        if y + height > screen_height:
            y = screen_height - height
        if x < 0:
            x = 0
        if y < 0:
            y = 0
        self.rect = Rect(x, y, width, height)

    def on_key(self, event) -> bool:
        if event.key == Key.UP:
            self.item_index = (self.item_index - 1) % len(self.items)
            self.invalidate()
        elif event.key == Key.DOWN:
            self.item_index = (self.item_index + 1) % len(self.items)
            self.invalidate()
        elif event.key == Key.ENTER:
            self.close(self.items[self.item_index][1])
        elif event.key == Key.ESCAPE:
            self.close(None)
        return True

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            if not self.rect.contains(event.x, event.y):
                self.close(None)
                return True
            index: int = event.y - self.rect.y - 1
            if 0 <= index < len(self.items):
                self.close(self.items[index][1])
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        style = self.theme_style("menu")
        chosen = self.theme_style("menu.selected")
        buffer.fill(self.rect, " ", style)
        buffer.box(self.rect, style, "", self.border)
        width: int = self.rect.width - 2
        i: int = 0
        while i < len(self.items):
            label: str = " " + self.items[i][0]
            if len(label) > width:
                label = label[:width]
            else:
                label = label + " " * (width - len(label))
            buffer.text(self.rect.x + 1, self.rect.y + 1 + i, label,
                        chosen if i == self.item_index else style)
            i += 1
