"""One-line status bar: left text plus right-aligned text."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer


class StatusBar(Widget):
    def __init__(self, text: str = "", right: str = "",
                 style_name: str = "status") -> None:
        super().__init__()
        self._text = text
        self._right = right
        self.style_name = style_name
        self.size_hint = 1

    @property
    def text(self) -> str:
        return self._text

    @text.setter
    def text(self, value: str) -> None:
        self._text = value
        self.invalidate()

    @property
    def right(self) -> str:
        return self._right

    @right.setter
    def right(self, value: str) -> None:
        self._right = value
        self.invalidate()

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        style = self.theme_style(self.style_name)
        width: int = self.rect.width
        line: str = self._text
        if len(line) > width:
            line = line[:width]
        else:
            line = line + " " * (width - len(line))
        if len(self._right) > 0 and len(self._right) < width:
            start: int = width - len(self._right)
            line = line[:start] + self._right
        buffer.text(self.rect.x, self.rect.y, line, style)
