"""Horizontal progress indicator with an optional percentage caption."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Shade


class ProgressBar(Widget):
    def __init__(self, value: int = 0, maximum: int = 100,
                 show_percent: bool = True, glyphs: str = "") -> None:
        """``glyphs`` is "<filled><empty>"; the CP437 shade blocks by
        default, ASCII (e.g. "#.") for golden tests."""
        super().__init__()
        self._value = value
        self.maximum = maximum
        self.show_percent = show_percent
        self.glyphs = glyphs if len(glyphs) >= 2 else Shade.FULL + Shade.LIGHT
        self.size_hint = 1

    @property
    def value(self) -> int:
        return self._value

    @value.setter
    def value(self, new_value: int) -> None:
        if new_value < 0:
            new_value = 0
        if new_value > self.maximum:
            new_value = self.maximum
        self._value = new_value
        self.invalidate()

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        style = self.theme_style("progress")
        width: int = self.rect.width
        filled: int = (width * self._value // self.maximum
                       if self.maximum > 0 else 0)
        bar: str = (self.glyphs[0] * filled
                    + self.glyphs[1] * (width - filled))
        if self.show_percent and width >= 5:
            percent: int = (100 * self._value // self.maximum
                            if self.maximum > 0 else 0)
            caption: str = " " + str(percent) + "% "
            start: int = (width - len(caption)) // 2
            bar = bar[:start] + caption + bar[start + len(caption):]
        buffer.text(self.rect.x, self.rect.y, bar, style)
