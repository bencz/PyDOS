"""Single-line text input with horizontal scrolling.

The value scrolls so the cursor stays visible; Insert toggles
overwrite; Enter fires on_submit(value); every edit fires
on_change(value).  Unhandled keys bubble up to the application.
"""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class TextInput(Widget):
    def __init__(self, value: str = "", placeholder: str = "",
                 max_length: int = 255, on_change=None,
                 on_submit=None) -> None:
        super().__init__()
        self._value = value
        self.placeholder = placeholder
        self.max_length = max_length
        self.on_change = on_change
        self.on_submit = on_submit
        self.cursor = len(value)
        self.offset = 0
        self.insert_mode = True
        self.focusable = True
        self.size_hint = 1

    @property
    def value(self) -> str:
        return self._value

    @value.setter
    def value(self, new_value: str) -> None:
        self._value = new_value
        self.cursor = len(new_value)
        self.offset = 0
        self.invalidate()

    def _changed(self) -> None:
        self.invalidate()
        if self.on_change is not None:
            self.on_change(self._value)

    def _ensure_visible(self) -> None:
        width: int = self.rect.width
        if width <= 1:
            return
        if self.cursor < self.offset:
            self.offset = self.cursor
        if self.cursor - self.offset > width - 1:
            self.offset = self.cursor - (width - 1)

    def insert_text(self, text: str) -> None:
        if len(self._value) + len(text) > self.max_length:
            return
        if self.insert_mode:
            self._value = (self._value[:self.cursor] + text
                           + self._value[self.cursor:])
        else:
            end: int = self.cursor + len(text)
            self._value = (self._value[:self.cursor] + text
                           + self._value[end:])
        self.cursor += len(text)
        self._changed()

    def backspace(self) -> None:
        if self.cursor > 0:
            self._value = (self._value[:self.cursor - 1]
                           + self._value[self.cursor:])
            self.cursor -= 1
            self._changed()

    def delete(self) -> None:
        if self.cursor < len(self._value):
            self._value = (self._value[:self.cursor]
                           + self._value[self.cursor + 1:])
            self._changed()

    def on_key(self, event) -> bool:
        key = event.key
        if key == Key.LEFT:
            if self.cursor > 0:
                self.cursor -= 1
                self.invalidate()
            return True
        if key == Key.RIGHT:
            if self.cursor < len(self._value):
                self.cursor += 1
                self.invalidate()
            return True
        if key == Key.HOME:
            self.cursor = 0
            self.invalidate()
            return True
        if key == Key.END:
            self.cursor = len(self._value)
            self.invalidate()
            return True
        if key == Key.BACKSPACE:
            self.backspace()
            return True
        if key == Key.DELETE:
            self.delete()
            return True
        if key == Key.INSERT:
            self.insert_mode = not self.insert_mode
            self.invalidate()
            return True
        if key == Key.ENTER:
            if self.on_submit is not None:
                self.on_submit(self._value)
                return True
            return False
        if key.is_printable():
            ch: str = key.name
            if key.shift and "a" <= ch <= "z":
                ch = chr(ord(ch) - 32)
            self.insert_text(ch)
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            position: int = self.offset + (event.x - self.rect.x)
            if position > len(self._value):
                position = len(self._value)
            if position < 0:
                position = 0
            self.cursor = position
            self.invalidate()
            return True
        return event.kind == EventType.MOUSE_DOWN

    def display_text(self) -> str:
        """The visible slice, padded to the widget width."""
        width: int = self.rect.width
        self._ensure_visible()
        if len(self._value) == 0 and not self.focused:
            shown: str = self.placeholder[:width]
            return shown + " " * (width - len(shown))
        visible: str = self._value[self.offset:self.offset + width]
        return visible + " " * (width - len(visible))

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        if len(self._value) == 0 and not self.focused:
            style = self.theme_style("input.placeholder")
        elif self.focused:
            style = self.theme_style("input.focus")
        else:
            style = self.theme_style("input")
        buffer.text(self.rect.x, self.rect.y, self.display_text(), style)

    def cursor_pos(self) -> tuple:
        if not self.focused:
            return (-1, -1, 0)
        self._ensure_visible()
        shape: int = 1 if self.insert_mode else 2
        return (self.rect.x + self.cursor - self.offset, self.rect.y, shape)
