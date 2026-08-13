"""Modal dialog: centered box with a drop shadow and its own focus ring.

Run through the application::

    choice = app.run_modal(dialog)   # returns dialog.result

``close(result)`` ends the nested loop.  Escape closes with None;
Enter activates ``default_button`` when no child consumed it.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Border
from pydos.tui.keys import Key


class Dialog(Widget):
    def __init__(self, child=None, title: str = "", width: int = 0,
                 height: int = 0) -> None:
        super().__init__()
        self.title = title
        self.want_width = width
        self.want_height = height
        self.open_flag = False
        self.result = None
        self.default_button = None
        self.border = Border.DOUBLE
        if child is not None:
            self.add(child)

    def close(self, result) -> None:
        self.result = result
        self.open_flag = False

    def place(self, screen_width: int, screen_height: int) -> None:
        """Center on screen using the requested size."""
        width: int = self.want_width
        height: int = self.want_height
        if width <= 0:
            width = 40
        if height <= 0:
            height = 8
        self.rect = Rect((screen_width - width) // 2,
                         (screen_height - height) // 2,
                         width, height)

    def layout(self) -> None:
        interior: Rect = self.rect.inset(1)
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            child.rect = Rect(interior.x, interior.y,
                              interior.width, interior.height)
            child.layout()
            i += 1

    def on_key(self, event) -> bool:
        if event.key == Key.ESCAPE:
            self.close(None)
            return True
        if event.key == Key.ENTER and self.default_button is not None:
            self.default_button.activate()
            return True
        return False

    def render_self(self, buffer: Buffer) -> None:
        style = self.theme_style("dialog")
        buffer.fill(self.rect, " ", style)
        buffer.box(self.rect, style, "", self.border)
        if len(self.title) > 0 and self.rect.width > 4:
            shown: str = " " + self.title + " "
            if len(shown) > self.rect.width - 2:
                shown = shown[:self.rect.width - 2]
            start: int = self.rect.x + (self.rect.width - len(shown)) // 2
            buffer.text(start, self.rect.y, shown,
                        self.theme_style("dialog.title"))
