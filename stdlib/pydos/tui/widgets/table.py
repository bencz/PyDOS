"""Columnar list with a header row and horizontal column scrolling."""

from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class Table(Widget):
    def __init__(self, columns: list, widths: list, rows: list,
                 selected: int = 0, on_select=None,
                 on_activate=None) -> None:
        super().__init__()
        self.columns = columns
        self.widths = widths
        self._rows = rows
        self._selected = selected
        self.top = 0
        self.first_column = 0
        self.on_select = on_select
        self.on_activate = on_activate
        self.focusable = True

    @property
    def rows(self) -> list:
        return self._rows

    @rows.setter
    def rows(self, value: list) -> None:
        self._rows = value
        self._selected = 0
        self.top = 0
        self.invalidate()

    @property
    def selected(self) -> int:
        return self._selected

    def select(self, index: int) -> None:
        if index < 0:
            index = 0
        if index >= len(self._rows):
            index = len(self._rows) - 1
        if index < 0 or index == self._selected:
            return
        self._selected = index
        self.invalidate()
        if self.on_select is not None:
            self.on_select(index)

    def _body_height(self) -> int:
        height: int = self.rect.height - 1
        return height if height > 0 else 0

    def _ensure_visible(self) -> None:
        height: int = self._body_height()
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
            self.select(len(self._rows) - 1)
            return True
        if key == Key.PAGE_UP:
            self.select(self._selected - self._body_height())
            return True
        if key == Key.PAGE_DOWN:
            self.select(self._selected + self._body_height())
            return True
        if key == Key.LEFT:
            if self.first_column > 0:
                self.first_column -= 1
                self.invalidate()
            return True
        if key == Key.RIGHT:
            if self.first_column + 1 < len(self.columns):
                self.first_column += 1
                self.invalidate()
            return True
        if key == Key.ENTER:
            if self.on_activate is not None and self._selected >= 0:
                self.on_activate(self._selected)
            return True
        return False

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            row: int = self.top + (event.y - self.rect.y) - 1
            if event.y > self.rect.y and 0 <= row < len(self._rows):
                if row == self._selected:
                    if self.on_activate is not None:
                        self.on_activate(row)
                else:
                    self.select(row)
            return True
        return event.kind == EventType.MOUSE_DOWN

    def _format_row(self, cells: list) -> str:
        parts: list = []
        column: int = self.first_column
        while column < len(self.columns):
            width: int = self.widths[column]
            cell: str = cells[column] if column < len(cells) else ""
            if len(cell) > width:
                cell = cell[:width]
            else:
                cell = cell + " " * (width - len(cell))
            parts.append(cell)
            column += 1
        line: str = " ".join(parts)
        if len(line) > self.rect.width:
            line = line[:self.rect.width]
        else:
            line = line + " " * (self.rect.width - len(line))
        return line

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty() or self.rect.height < 2:
            return
        self._ensure_visible()
        base = self.theme_style("list")
        chosen = self.theme_style("list.selected")
        header = self.theme_style("table.header")
        buffer.fill(self.rect, " ", base)
        buffer.text(self.rect.x, self.rect.y,
                    self._format_row(self.columns), header)
        row: int = 0
        while (row < self._body_height()
               and self.top + row < len(self._rows)):
            index: int = self.top + row
            style = chosen if index == self._selected else base
            buffer.text(self.rect.x, self.rect.y + 1 + row,
                        self._format_row(self._rows[index]), style)
            row += 1
