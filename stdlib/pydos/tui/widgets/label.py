"""Static single-line text."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer


class Label(Widget):
    def __init__(self, text: str = "", style_name: str = "label") -> None:
        super().__init__()
        self._text = text
        self.style_name = style_name
        self.size_hint = 1

    @property
    def text(self) -> str:
        return self._text

    @text.setter
    def text(self, value: str) -> None:
        self._text = value
        self.invalidate()

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        shown: str = self._text
        if len(shown) > self.rect.width:
            shown = shown[:self.rect.width]
        buffer.text(self.rect.x, self.rect.y, shown,
                    self.theme_style(self.style_name))
