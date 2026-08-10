from pydos.io.files import read_text, write_text


class TextBuffer:
    def __init__(self, path):
        self.lines = []
        self.row = 0
        self.column = 0
        self.goal_column = 0
        self.dirty = False
        self.exists = False
        self.load(path)

    def load(self, path):
        self.path = path
        try:
            content = read_text(path)
            self.lines = content.splitlines()
            self.exists = True
        except FileNotFoundError:
            self.lines = []
            self.exists = False
        if len(self.lines) == 0:
            self.lines.append("")
        self.row = 0
        self.column = 0
        self.goal_column = 0
        self.dirty = False

    def new_document(self, path="UNTITLED.TXT"):
        self.path = path
        self.lines = [""]
        self.row = 0
        self.column = 0
        self.goal_column = 0
        self.dirty = False
        self.exists = False

    def current_line(self):
        return self.lines[self.row]

    def clamp_column(self):
        line = self.current_line()
        self.column = self.goal_column
        if self.column > len(line):
            self.column = len(line)

    def move_left(self):
        if self.column > 0:
            self.column -= 1
        elif self.row > 0:
            self.row -= 1
            self.column = len(self.current_line())
        self.goal_column = self.column

    def move_right(self):
        if self.column < len(self.current_line()):
            self.column += 1
        elif self.row + 1 < len(self.lines):
            self.row += 1
            self.column = 0
        self.goal_column = self.column

    def move_up(self):
        if self.row > 0:
            self.row -= 1
            self.clamp_column()

    def move_down(self):
        if self.row + 1 < len(self.lines):
            self.row += 1
            self.clamp_column()

    def move_home(self):
        self.column = 0
        self.goal_column = self.column

    def move_end(self):
        self.column = len(self.current_line())
        self.goal_column = self.column

    def page_up(self, rows=10):
        self.row -= rows
        if self.row < 0:
            self.row = 0
        self.clamp_column()

    def page_down(self, rows=10):
        self.row += rows
        if self.row >= len(self.lines):
            self.row = len(self.lines) - 1
        self.clamp_column()

    def go_to_line(self, line_number):
        if line_number < 1:
            line_number = 1
        if line_number > len(self.lines):
            line_number = len(self.lines)
        self.row = line_number - 1
        self.clamp_column()

    def insert(self, char, insert_mode=True):
        line = self.current_line()
        if insert_mode:
            self.lines[self.row] = (line[:self.column] + char
                                    + line[self.column:])
        else:
            end = self.column + len(char)
            self.lines[self.row] = (line[:self.column] + char
                                    + line[end:])
        self.column += len(char)
        self.goal_column = self.column
        self.dirty = True

    def insert_tab(self, tab_size=4, insert_mode=True):
        count = tab_size - self.column % tab_size
        self.insert(" " * count, insert_mode)

    def newline(self):
        line = self.current_line()
        left = line[:self.column]
        right = line[self.column:]
        self.lines[self.row] = left
        self.lines.insert(self.row + 1, right)
        self.row += 1
        self.column = 0
        self.goal_column = 0
        self.dirty = True

    def backspace(self):
        if self.column > 0:
            line = self.current_line()
            self.lines[self.row] = (line[:self.column - 1]
                                    + line[self.column:])
            self.column -= 1
            self.goal_column = self.column
            self.dirty = True
        elif self.row > 0:
            previous = self.lines[self.row - 1]
            current = self.lines.pop(self.row)
            self.row -= 1
            self.column = len(previous)
            self.goal_column = self.column
            self.lines[self.row] = previous + current
            self.dirty = True

    def delete(self):
        line = self.current_line()
        if self.column < len(line):
            self.lines[self.row] = (line[:self.column]
                                    + line[self.column + 1:])
            self.dirty = True
        elif self.row + 1 < len(self.lines):
            following = self.lines.pop(self.row + 1)
            self.lines[self.row] = line + following
            self.dirty = True

    def find(self, query, case_sensitive=False, start_after=False):
        if len(query) == 0:
            return False
        wanted = query
        if not case_sensitive:
            wanted = query.lower()
        row = self.row
        start = self.column
        if start_after:
            start += 1
        checked = 0
        while checked < len(self.lines):
            source = self.lines[row]
            searchable = source
            if not case_sensitive:
                searchable = source.lower()
            found = searchable.find(wanted, start)
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

    def replace_at_cursor(self, query, replacement, case_sensitive=False):
        if len(query) == 0:
            return False
        line = self.current_line()
        found = line[self.column:self.column + len(query)]
        matches = found == query
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

    def save(self):
        write_text(self.path, "\r\n".join(self.lines) + "\r\n")
        self.dirty = False
        self.exists = True

    def save_as(self, path):
        self.path = path
        self.save()
