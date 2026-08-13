"""Toggle: Space or a mouse click flips ``checked``."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class CheckBox(Widget):
    def __init__(self, text: str = "", checked: bool = False,
                 on_change=None) -> None:
        super().__init__()
        self.text = text
        self._checked = checked
        self.on_change = on_change
        self.focusable = True
        self.size_hint = 1

    @property
    def checked(self) -> bool:
        return self._checked

    @checked.setter
    def checked(self, value: bool) -> None:
        self._checked = value
        self.invalidate()

    def toggle(self) -> None:
        self._checked = not self._checked
        self.invalidate()
        if self.on_change is not None:
            self.on_change(self._checked)

    def on_key(self, event) -> bool:
        if event.key == Key.SPACE:
            self.toggle()
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            self.toggle()
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        style = self.theme_style("check.focus" if self.focused else "check")
        mark: str = "X" if self._checked else " "
        shown: str = "[" + mark + "] " + self.text
        if len(shown) > self.rect.width:
            shown = shown[:self.rect.width]
        buffer.fill(self.rect, " ", style)
        buffer.text(self.rect.x, self.rect.y, shown, style)
