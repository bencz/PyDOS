"""Push button: Enter/Space or a mouse click fires on_click."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class Button(Widget):
    def __init__(self, text: str = "", on_click=None) -> None:
        super().__init__()
        self._text = text
        self.on_click = on_click
        self.focusable = True
        self.size_hint = 1

    @property
    def text(self) -> str:
        return self._text

    @text.setter
    def text(self, value: str) -> None:
        self._text = value
        self.invalidate()

    def activate(self) -> None:
        if self.on_click is not None:
            self.on_click()

    def on_key(self, event) -> bool:
        if event.key == Key.ENTER or event.key == Key.SPACE:
            self.activate()
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            self.activate()
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        style = self.theme_style("button.focus" if self.focused else "button")
        caption: str = "[ " + self._text + " ]"
        if len(caption) > self.rect.width:
            caption = caption[:self.rect.width]
        pad: int = (self.rect.width - len(caption)) // 2
        if pad < 0:
            pad = 0
        buffer.fill(self.rect, " ", style)
        buffer.text(self.rect.x + pad, self.rect.y, caption, style)
