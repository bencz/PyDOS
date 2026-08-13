"""Multi-line text editing: a pure document model plus its widget.

``TextDocument`` is the editing model the EDIT sample proved (cursor
movement with a goal column, insert/overwrite, line joins, wrapping
search, replace-at-cursor).  It knows nothing about screens or files —
subclass it to add persistence, as the sample's TextBuffer does.

``TextArea`` renders a document through a scrolling viewport with an
optional line-number gutter, current-line and search-match highlights,
and an automatic scroll indicator.  Editing keys are handled here;
function keys and Ctrl shortcuts bubble up to the application's
bindings.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.events import EventType
from pydos.tui.widgets.scrollbr import ScrollBar


class TextDocument:
    def __init__(self) -> None:
        self.lines = [""]
        self.row = 0
        self.column = 0
        self.goal_column = 0
        self.dirty = False

    def set_text(self, content: str) -> None:
        self.lines = content.splitlines()
        if len(self.lines) == 0:
            self.lines.append("")
        self.row = 0
        self.column = 0
        self.goal_column = 0
        self.dirty = False

    def text(self) -> str:
        return "\n".join(self.lines)

    def current_line(self) -> str:
        return self.lines[self.row]

    def clamp_column(self) -> None:
        line: str = self.current_line()
        self.column = self.goal_column
        if self.column > len(line):
            self.column = len(line)

    def move_left(self) -> None:
        if self.column > 0:
            self.column -= 1
        elif self.row > 0:
            self.row -= 1
            self.column = len(self.current_line())
        self.goal_column = self.column

    def move_right(self) -> None:
        if self.column < len(self.current_line()):
            self.column += 1
        elif self.row + 1 < len(self.lines):
            self.row += 1
            self.column = 0
        self.goal_column = self.column

    def move_up(self) -> None:
        if self.row > 0:
            self.row -= 1
            self.clamp_column()

    def move_down(self) -> None:
        if self.row + 1 < len(self.lines):
            self.row += 1
            self.clamp_column()

    def move_home(self) -> None:
        self.column = 0
        self.goal_column = self.column

    def move_end(self) -> None:
        self.column = len(self.current_line())
        self.goal_column = self.column

    def page_up(self, rows: int = 10) -> None:
        self.row -= rows
        if self.row < 0:
            self.row = 0
        self.clamp_column()

    def page_down(self, rows: int = 10) -> None:
        self.row += rows
        if self.row >= len(self.lines):
            self.row = len(self.lines) - 1
        self.clamp_column()

    def go_to_line(self, line_number: int) -> None:
        if line_number < 1:
            line_number = 1
        if line_number > len(self.lines):
            line_number = len(self.lines)
        self.row = line_number - 1
        self.clamp_column()

    def insert(self, char: str, insert_mode: bool = True) -> None:
        line: str = self.current_line()
        if insert_mode:
            self.lines[self.row] = (line[:self.column] + char
                                    + line[self.column:])
        else:
            end: int = self.column + len(char)
            self.lines[self.row] = (line[:self.column] + char
                                    + line[end:])
        self.column += len(char)
        self.goal_column = self.column
        self.dirty = True

    def insert_tab(self, tab_size: int = 4,
                   insert_mode: bool = True) -> None:
        count: int = tab_size - self.column % tab_size
        self.insert(" " * count, insert_mode)

    def newline(self) -> None:
        line: str = self.current_line()
        left: str = line[:self.column]
        right: str = line[self.column:]
        self.lines[self.row] = left
        self.lines.insert(self.row + 1, right)
        self.row += 1
        self.column = 0
        self.goal_column = 0
        self.dirty = True

    def backspace(self) -> None:
        if self.column > 0:
            line: str = self.current_line()
            self.lines[self.row] = (line[:self.column - 1]
                                    + line[self.column:])
            self.column -= 1
            self.goal_column = self.column
            self.dirty = True
        elif self.row > 0:
            previous: str = self.lines[self.row - 1]
            current: str = self.lines.pop(self.row)
            self.row -= 1
            self.column = len(previous)
            self.goal_column = self.column
            self.lines[self.row] = previous + current
            self.dirty = True

    def delete(self) -> None:
        line: str = self.current_line()
        if self.column < len(line):
            self.lines[self.row] = (line[:self.column]
                                    + line[self.column + 1:])
            self.dirty = True
        elif self.row + 1 < len(self.lines):
            following: str = self.lines.pop(self.row + 1)
            self.lines[self.row] = line + following
            self.dirty = True

    def find(self, query: str, case_sensitive: bool = False,
             start_after: bool = False) -> bool:
        if len(query) == 0:
            return False
        wanted: str = query
        if not case_sensitive:
            wanted = query.lower()
        row: int = self.row
        start: int = self.column
        if start_after:
            start += 1
        checked: int = 0
        while checked < len(self.lines):
            source: str = self.lines[row]
            searchable: str = source
            if not case_sensitive:
                searchable = source.lower()
            found: int = searchable.find(wanted, start)
            if found >= 0:
                self.row = row
                self.column = found
                self.goal_column = found
                return True
            row += 1
            if row >= len(self.lines):
                row = 0
            start = 0
            checked += 1
        return False

    def replace_at_cursor(self, query: str, replacement: str,
                          case_sensitive: bool = False) -> bool:
        if len(query) == 0:
            return False
        line: str = self.current_line()
        found: str = line[self.column:self.column + len(query)]
        matches: bool = found == query
        if not case_sensitive:
            matches = found.lower() == query.lower()
        if not matches:
            return False
        self.lines[self.row] = (line[:self.column] + replacement
                                + line[self.column + len(query):])
        self.column += len(replacement)
        self.goal_column = self.column
        self.dirty = True
        return True


class TextArea(Widget):
    def __init__(self, document=None, gutter: bool = False,
                 on_change=None) -> None:
        super().__init__()
        self.doc = document if document is not None else TextDocument()
        self._gutter = gutter
        self.on_change = on_change
        self.top_row = 0
        self.left_column = 0
        self.insert_mode = True
        self.show_scrollbar = True
        self.scroll_glyphs = ""
        self.highlight_text = ""
        self.highlight_case = False
        self.focusable = True

    @property
    def gutter(self) -> bool:
        return self._gutter

    @gutter.setter
    def gutter(self, value: bool) -> None:
        self._gutter = value
        self.invalidate()

    def _changed(self) -> None:
        self.invalidate()
        if self.on_change is not None:
            self.on_change()

    def _gutter_width(self) -> int:
        return 6 if self._gutter else 0

    def _text_width(self) -> int:
        width: int = self.rect.width - self._gutter_width()
        if self._overflows():
            width -= 1
        return width if width > 0 else 0

    def _overflows(self) -> bool:
        return (self.show_scrollbar
                and len(self.doc.lines) > self.rect.height)

    def ensure_visible(self) -> None:
        height: int = self.rect.height
        if height <= 0:
            return
        if self.doc.row < self.top_row:
            self.top_row = self.doc.row
        if self.doc.row >= self.top_row + height:
            self.top_row = self.doc.row - height + 1
        width: int = self._text_width()
        if width <= 1:
            return
        if self.doc.column < self.left_column:
            self.left_column = self.doc.column
        if self.doc.column - self.left_column > width - 1:
            self.left_column = self.doc.column - (width - 1)

    def on_key(self, event) -> bool:
        key = event.key
        doc = self.doc
        if key == Key.UP:
            doc.move_up()
        elif key == Key.DOWN:
            doc.move_down()
        elif key == Key.LEFT:
            doc.move_left()
        elif key == Key.RIGHT:
            doc.move_right()
        elif key == Key.HOME:
            doc.move_home()
        elif key == Key.END:
            doc.move_end()
        elif key == Key.PAGE_UP:
            doc.page_up(self.rect.height)
        elif key == Key.PAGE_DOWN:
            doc.page_down(self.rect.height)
        elif key == Key.ENTER:
            doc.newline()
            self._changed()
        elif key == Key.BACKSPACE:
            doc.backspace()
            self._changed()
        elif key == Key.DELETE:
            doc.delete()
            self._changed()
        elif key == Key.TAB:
            doc.insert_tab(4, self.insert_mode)
            self._changed()
        elif key == Key.INSERT:
            self.insert_mode = not self.insert_mode
        elif key.is_printable():
            ch: str = key.name
            if key.shift and "a" <= ch <= "z":
                ch = chr(ord(ch) - 32)
            doc.insert(ch, self.insert_mode)
            self._changed()
        else:
            return False
        self.invalidate()
        return True

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            row: int = self.top_row + (event.y - self.rect.y)
            if row >= len(self.doc.lines):
                row = len(self.doc.lines) - 1
            if row < 0:
                row = 0
            self.doc.row = row
            column: int = (self.left_column + (event.x - self.rect.x)
                           - self._gutter_width())
            if column < 0:
                column = 0
            line: str = self.doc.current_line()
            if column > len(line):
                column = len(line)
            self.doc.column = column
            self.doc.goal_column = column
            self.invalidate()
            return True
        return event.kind == EventType.MOUSE_DOWN

    def _match_span(self, row_index: int) -> tuple:
        """(start, length) of the highlight in this row, or (-1, 0)."""
        if len(self.highlight_text) == 0:
            return (-1, 0)
        if row_index != self.doc.row:
            return (-1, 0)
        line: str = self.doc.lines[row_index]
        piece: str = line[self.doc.column:
                          self.doc.column + len(self.highlight_text)]
        matches: bool = piece == self.highlight_text
        if not self.highlight_case:
            matches = piece.lower() == self.highlight_text.lower()
        if matches and len(piece) == len(self.highlight_text):
            return (self.doc.column, len(piece))
        return (-1, 0)

    def render_self(self, buffer: Buffer) -> None:
        if self.rect.is_empty():
            return
        self.ensure_visible()
        base = self.theme_style("textarea")
        current = self.theme_style("textarea.current")
        gutter_style = self.theme_style("textarea.gutter")
        match_style = self.theme_style("textarea.match")
        buffer.fill(self.rect, " ", base)

        gutter_width: int = self._gutter_width()
        text_width: int = self._text_width()
        text_x: int = self.rect.x + gutter_width
        row: int = 0
        while (row < self.rect.height
               and self.top_row + row < len(self.doc.lines)):
            index: int = self.top_row + row
            y: int = self.rect.y + row
            line_style = current if index == self.doc.row else base
            if gutter_width > 0:
                number: str = str(index + 1)
                if len(number) > 5:
                    number = number[:5]
                buffer.text(self.rect.x, y,
                            " " * (5 - len(number)) + number + " ",
                            gutter_style)
            line: str = self.doc.lines[index]
            visible: str = line[self.left_column:
                                self.left_column + text_width]
            visible = visible + " " * (text_width - len(visible))
            buffer.text(text_x, y, visible, line_style)
            span: tuple = self._match_span(index)
            if span[0] >= 0:
                start: int = span[0] - self.left_column
                if 0 <= start < text_width:
                    length: int = span[1]
                    if start + length > text_width:
                        length = text_width - start
                    buffer.text(text_x + start, y,
                                line[span[0]:span[0] + length],
                                match_style)
            row += 1

        if self._overflows():
            ScrollBar.draw_into(
                buffer,
                Rect(self.rect.right() - 1, self.rect.y, 1,
                     self.rect.height),
                len(self.doc.lines), self.rect.height, self.top_row,
                self.theme_style("scrollbar"), self.scroll_glyphs,
            )

    def cursor_pos(self) -> tuple:
        if not self.focused:
            return (-1, -1, 0)
        self.ensure_visible()
        shape: int = 1 if self.insert_mode else 2
        x: int = (self.rect.x + self._gutter_width()
                  + self.doc.column - self.left_column)
        y: int = self.rect.y + self.doc.row - self.top_row
        return (x, y, shape)
