"""Full-screen, multi-file-style DOS text editor sample."""

from buffer import TextBuffer
from menus import MenuBar
from theme import EditorTheme
from pydos.io.tui import Canvas, Dialog, Key, Screen, TextInput, wait_key


class Editor:
    def __init__(self, path):
        self.buffer = TextBuffer(path)
        self.screen = Screen(EditorTheme.DESKTOP_FG,
                             EditorTheme.DESKTOP_BG)
        self.menu = MenuBar()
        self.top_row = 0
        self.left_column = 0
        self.line_numbers = True
        self.insert_mode = True
        self.case_sensitive = False
        self.last_search = ""
        self.running = True
        self.full_redraw = True
        if self.buffer.exists:
            self.message = "Loaded " + self.buffer.path
        else:
            self.message = "New file - press F2 to save"

    def visible_rows(self):
        return self.screen.height - 5

    def text_x(self):
        if self.line_numbers:
            return 9
        return 2

    def text_width(self):
        return self.screen.width - self.text_x() - 2

    def ensure_visible(self):
        rows = self.visible_rows()
        width = self.text_width()
        if self.buffer.row < self.top_row:
            self.top_row = self.buffer.row
        elif self.buffer.row >= self.top_row + rows:
            self.top_row = self.buffer.row - rows + 1
        if self.buffer.column < self.left_column:
            self.left_column = self.buffer.column
        elif self.buffer.column >= self.left_column + width:
            self.left_column = self.buffer.column - width + 1
        if self.top_row < 0:
            self.top_row = 0
        if self.left_column < 0:
            self.left_column = 0

    def center_cursor(self):
        self.top_row = self.buffer.row - self.visible_rows() // 2
        if self.top_row < 0:
            self.top_row = 0
        max_top = len(self.buffer.lines) - self.visible_rows()
        if max_top < 0:
            max_top = 0
        if self.top_row > max_top:
            self.top_row = max_top
        self.ensure_visible()

    def title_text(self):
        marker = ""
        if self.buffer.dirty:
            marker = " *"
        return "EDIT  " + self.buffer.path + marker

    def draw_title_border(self):
        title = " " + self.title_text() + " "
        available = self.screen.width - 3
        if len(title) > available:
            title = title[:available]
        value = "+-" + title
        value += "-" * (self.screen.width - len(value) - 1) + "+"
        self.screen.write(0, 1, value, EditorTheme.DESKTOP_FG,
                          EditorTheme.DESKTOP_BG)

    def draw_menu_bar(self, selected=-1):
        width = self.screen.width
        value = " File  Edit  Search  View  Help".ljust(width)
        self.screen.write(0, 0, value, EditorTheme.MENU_FG,
                          EditorTheme.MENU_BG)
        if selected >= 0:
            menu = self.menu.menus[selected]
            self.screen.write(menu.x, 0, menu.title,
                              EditorTheme.MENU_SELECTED_FG,
                              EditorTheme.MENU_SELECTED_BG)

    def draw_scrollbar(self, canvas):
        rows = self.visible_rows()
        x = self.screen.width - 2
        canvas.draw_text(x, 2, "^")
        canvas.draw_text(x, 2 + rows - 1, "v")
        track = rows - 2
        offset = 0
        max_top = len(self.buffer.lines) - rows
        if max_top > 0 and track > 1:
            offset = self.top_row * (track - 1) // max_top
        canvas.draw_text(x, 3 + offset, "#")

    def draw_document(self, canvas):
        rows = self.visible_rows()
        x = self.text_x()
        width = self.text_width()
        shown_row = 0
        while shown_row < rows:
            source_row = self.top_row + shown_row
            screen_row = 2 + shown_row
            if source_row < len(self.buffer.lines):
                if self.line_numbers:
                    number = str(source_row + 1).rjust(5) + " "
                    canvas.draw_text(1, screen_row, number)
                line = self.buffer.lines[source_row]
                canvas.draw_text(x, screen_row,
                                 line[self.left_column:
                                      self.left_column + width])
            shown_row += 1
        self.draw_scrollbar(canvas)

    def draw_current_line(self):
        self.draw_line(self.buffer.row, True)

    def draw_line(self, source_row, current=False):
        screen_row = 2 + source_row - self.top_row
        if screen_row < 2 or screen_row >= 2 + self.visible_rows():
            return
        fg = EditorTheme.DESKTOP_FG
        bg = EditorTheme.DESKTOP_BG
        if current:
            fg = EditorTheme.CURRENT_FG
            bg = EditorTheme.CURRENT_BG
        x = self.text_x()
        width = self.text_width()
        line = ""
        if source_row >= 0 and source_row < len(self.buffer.lines):
            line = self.buffer.lines[source_row]
        shown = line[self.left_column:self.left_column + width]
        self.screen.write(x, screen_row, shown.ljust(width), fg, bg)
        if self.line_numbers:
            number = "      "
            if source_row >= 0 and source_row < len(self.buffer.lines):
                number = str(source_row + 1).rjust(5) + " "
            self.screen.write(1, screen_row, number, fg, bg)
        if not current:
            return
        screen_row = 2 + self.buffer.row - self.top_row
        if len(self.last_search) > 0:
            found = line[self.buffer.column:
                         self.buffer.column + len(self.last_search)]
            matches = found == self.last_search
            if not self.case_sensitive:
                matches = found.lower() == self.last_search.lower()
            match_x = x + self.buffer.column - self.left_column
            if matches and match_x >= x and match_x < x + width:
                visible_match = found[:x + width - match_x]
                self.screen.write(match_x, screen_row, visible_match,
                                  EditorTheme.MATCH_FG,
                                  EditorTheme.MATCH_BG)

    def place_cursor(self):
        self.screen.show_cursor()
        cursor_x = (self.text_x() + self.buffer.column
                    - self.left_column)
        cursor_y = 2 + self.buffer.row - self.top_row
        self.screen.move_cursor(cursor_x, cursor_y)

    def draw_incremental(self, previous_row):
        self.draw_title_border()
        if previous_row != self.buffer.row:
            self.draw_line(previous_row, False)
        self.draw_current_line()
        self.draw_status()
        self.place_cursor()

    def draw_status(self):
        mode = "INS"
        if not self.insert_mode:
            mode = "OVR"
        percent = (self.buffer.row + 1) * 100 // len(self.buffer.lines)
        status = (" Ln " + str(self.buffer.row + 1)
                  + "/" + str(len(self.buffer.lines))
                  + "  Col " + str(self.buffer.column + 1)
                  + "  " + mode + "  " + str(percent) + "%  "
                  + self.message)
        self.screen.write(0, self.screen.height - 2,
                          status[:self.screen.width].ljust(self.screen.width),
                          EditorTheme.STATUS_FG, EditorTheme.STATUS_BG)
        shortcuts = " F1 Help  F2 Save  F3 Find  F5 Goto  F6 Replace  F10 Menu "
        self.screen.write(0, self.screen.height - 1,
                          shortcuts.ljust(self.screen.width),
                          EditorTheme.SHORTCUT_FG,
                          EditorTheme.SHORTCUT_BG)

    def draw(self, show_cursor=True):
        width = self.screen.width
        height = self.screen.height
        canvas = Canvas(width, height, " ")
        canvas.draw_box(0, 1, width, height - 3, self.title_text())
        if self.line_numbers:
            canvas.draw_vline(7, 2, self.visible_rows(), "|")
        self.draw_document(canvas)
        self.screen.present(canvas, EditorTheme.DESKTOP_FG,
                            EditorTheme.DESKTOP_BG)
        self.draw_current_line()
        self.draw_menu_bar()
        self.draw_status()
        if show_cursor:
            self.place_cursor()
        else:
            self.screen.hide_cursor()

    def draw_dialog(self, title, lines, width, height):
        self.draw(False)
        x = (self.screen.width - width) // 2
        y = (self.screen.height - height) // 2
        shadow = " " * width
        self.screen.write(x + 2, y + height, shadow,
                          EditorTheme.SHADOW_FG, EditorTheme.SHADOW_BG)
        row = 1
        while row <= height:
            self.screen.write(x + width, y + row, "  ",
                              EditorTheme.SHADOW_FG,
                              EditorTheme.SHADOW_BG)
            row += 1
        pane = Canvas(width, height, " ")
        dialog = Dialog(0, 0, width, height, title, lines)
        dialog.draw(pane)
        row = 0
        while row < height:
            self.screen.write(x, y + row, pane.get_line(row),
                              EditorTheme.DIALOG_FG,
                              EditorTheme.DIALOG_BG)
            row += 1
        return [x, y]

    def input_dialog(self, title, label, initial="", max_length=255):
        width = 58
        height = 7
        x = (self.screen.width - width) // 2
        y = (self.screen.height - height) // 2
        field = TextInput(x + 2, y + 3, width - 4, initial, max_length)
        self.draw_dialog(title,
                         [label, "", "", "Enter accepts; Esc cancels"],
                         width, height)
        while True:
            self.screen.write(field.x, field.y, field.display_text(),
                              EditorTheme.INPUT_FG, EditorTheme.INPUT_BG)
            self.screen.show_cursor()
            self.screen.move_cursor(field.cursor_x(), field.y)
            key = wait_key()
            if key == Key.ENTER:
                return field.value
            if key == Key.ESCAPE:
                return None
            field.handle_key(key)

    def message_dialog(self, title, lines, width=60, height=12):
        lines.append("")
        lines.append("Press any key to return")
        self.draw_dialog(title, lines, width, height)
        wait_key()

    def confirm_changes(self):
        if not self.buffer.dirty:
            return True
        lines = [
            "The document has unsaved changes.",
            "",
            "Y  Save changes",
            "N  Discard changes",
            "Esc  Cancel",
        ]
        while True:
            self.draw_dialog("Unsaved document", lines, 50, 9)
            key = wait_key()
            if key == 89 or key == 121:
                return self.save_buffer()
            if key == 78 or key == 110:
                return True
            if key == Key.ESCAPE:
                return False

    def save_buffer(self):
        if self.buffer.path == "UNTITLED.TXT":
            return self.save_as()
        try:
            self.buffer.save()
            self.message = "Saved " + self.buffer.path
            return True
        except OSError as error:
            self.message_dialog("Save error", [str(error)], 60, 7)
            return False

    def save_as(self):
        path = self.input_dialog("Save as", "File name:", self.buffer.path, 72)
        if path is None or len(path.strip()) == 0:
            return False
        try:
            self.buffer.save_as(path.strip())
            self.message = "Saved as " + self.buffer.path
            return True
        except OSError as error:
            self.message_dialog("Save error", [str(error)], 60, 7)
            return False

    def new_document(self):
        if self.confirm_changes():
            self.buffer.new_document()
            self.top_row = 0
            self.left_column = 0
            self.last_search = ""
            self.message = "New document"

    def open_document(self):
        if not self.confirm_changes():
            return
        path = self.input_dialog("Open", "File name:", self.buffer.path, 72)
        if path is None or len(path.strip()) == 0:
            return
        self.buffer.load(path.strip())
        self.top_row = 0
        self.left_column = 0
        self.last_search = ""
        if self.buffer.exists:
            self.message = "Loaded " + self.buffer.path
        else:
            self.message = "File not found; editing a new document"

    def find_text(self):
        query = self.input_dialog("Find", "Find what:", self.last_search, 60)
        if query is None or len(query) == 0:
            return
        self.last_search = query
        if self.buffer.find(query, self.case_sensitive, False):
            self.message = "Found: " + query
            self.ensure_visible()
        else:
            self.message = "Not found: " + query

    def find_next(self):
        if len(self.last_search) == 0:
            self.find_text()
            return
        if self.buffer.find(self.last_search, self.case_sensitive, True):
            self.message = "Found next: " + self.last_search
            self.ensure_visible()
        else:
            self.message = "Not found: " + self.last_search

    def replace_text(self):
        query = self.input_dialog("Replace", "Find what:",
                                  self.last_search, 60)
        if query is None or len(query) == 0:
            return
        replacement = self.input_dialog("Replace", "Replace with:", "", 60)
        if replacement is None:
            return
        self.last_search = query
        if not self.buffer.replace_at_cursor(query, replacement,
                                             self.case_sensitive):
            if not self.buffer.find(query, self.case_sensitive, False):
                self.message = "Not found: " + query
                return
            self.buffer.replace_at_cursor(query, replacement,
                                          self.case_sensitive)
        self.message = "Replaced one occurrence"
        self.ensure_visible()

    def go_to_line(self):
        value = self.input_dialog("Go to line", "Line number:",
                                  str(self.buffer.row + 1), 8)
        if value is None:
            return
        value = value.strip()
        if not value.isdigit():
            self.message = "Line number must contain digits"
            return
        self.buffer.go_to_line(int(value))
        self.center_cursor()
        self.message = "Moved to line " + str(self.buffer.row + 1)

    def show_help(self):
        self.message_dialog("EDIT help", [
            "Cursor keys       Move through the document",
            "Home/End          Start/end of line",
            "PgUp/PgDn         Move one editor page",
            "Ins               Insert/overwrite mode",
            "F2 / Ctrl+S       Save",
            "F3 / Ctrl+F       Find",
            "F4                Find next",
            "F5 / Ctrl+G       Go to line",
            "F6                Replace one occurrence",
            "F10 / Alt+key     Open the menu bar",
            "Ctrl+N/O/Q        New, open, exit",
        ], 68, 16)

    def show_about(self):
        self.message_dialog("About PyDOS EDIT", [
            "PyDOS EDIT",
            "A full-screen editor written in Python and compiled for DOS.",
            "",
            "The UI, menus, dialogs and editor model live in Python.",
            "Only BIOS screen and keyboard primitives live in the C runtime.",
            "",
            "Targets: Intel 8086 real mode and 80386 protected mode.",
        ], 70, 13)

    def execute_command(self, command):
        self.full_redraw = True
        if command == "new":
            self.new_document()
        elif command == "open":
            self.open_document()
        elif command == "save":
            self.save_buffer()
        elif command == "save_as":
            self.save_as()
        elif command == "exit":
            if self.confirm_changes():
                self.running = False
        elif command == "insert_mode":
            self.insert_mode = not self.insert_mode
            self.message = "Editing mode changed"
        elif command == "delete":
            self.buffer.delete()
        elif command == "newline":
            self.buffer.newline()
        elif command == "find":
            self.find_text()
        elif command == "find_next":
            self.find_next()
        elif command == "replace":
            self.replace_text()
        elif command == "goto":
            self.go_to_line()
        elif command == "match_case":
            self.case_sensitive = not self.case_sensitive
            self.message = "Match case toggled"
        elif command == "line_numbers":
            self.line_numbers = not self.line_numbers
            self.left_column = 0
            self.message = "Line numbers toggled"
        elif command == "center_cursor":
            self.center_cursor()
        elif command == "help":
            self.show_help()
        elif command == "about":
            self.show_about()
        self.ensure_visible()

    def draw_menu(self):
        self.draw(False)
        self.draw_menu_bar(self.menu.menu_index)
        menu = self.menu.current_menu()
        height = len(menu.items) + 2
        pane = Canvas(menu.width, height, " ")
        pane.draw_box(0, 0, menu.width, height)
        row = 0
        while row < len(menu.items):
            text = menu.items[row].display_text(menu.width - 2)
            pane.draw_text(1, 1 + row, text)
            row += 1
        row = 0
        while row < height:
            self.screen.write(menu.x - 1, 1 + row, pane.get_line(row),
                              EditorTheme.DIALOG_FG,
                              EditorTheme.DIALOG_BG)
            row += 1
        selected = menu.items[self.menu.item_index]
        text = selected.display_text(menu.width - 2)
        if selected.command == "match_case" and self.case_sensitive:
            text = "*" + text[1:]
        if selected.command == "line_numbers" and self.line_numbers:
            text = "*" + text[1:]
        self.screen.write(menu.x, 2 + self.menu.item_index, text,
                          EditorTheme.MENU_SELECTED_FG,
                          EditorTheme.MENU_SELECTED_BG)

    def run_menu(self, menu_index=0):
        self.menu.open(menu_index)
        while self.menu.active:
            self.draw_menu()
            command = self.menu.handle_key(wait_key())
            if command is not None:
                self.execute_command(command)

    def handle_key(self, key):
        self.full_redraw = False
        if key == Key.F10:
            self.run_menu(0)
            self.full_redraw = True
        elif key == Key.ALT_F:
            self.run_menu(0)
            self.full_redraw = True
        elif key == Key.ALT_E:
            self.run_menu(1)
            self.full_redraw = True
        elif key == Key.ALT_S:
            self.run_menu(2)
            self.full_redraw = True
        elif key == Key.ALT_V:
            self.run_menu(3)
            self.full_redraw = True
        elif key == Key.ALT_H:
            self.run_menu(4)
            self.full_redraw = True
        elif key == Key.F1:
            self.show_help()
            self.full_redraw = True
        elif key == Key.F2 or key == Key.CTRL_S:
            self.save_buffer()
            self.full_redraw = True
        elif key == Key.F3 or key == Key.CTRL_F:
            self.find_text()
            self.full_redraw = True
        elif key == Key.F4:
            self.find_next()
            self.full_redraw = True
        elif key == Key.F5 or key == Key.CTRL_G:
            self.go_to_line()
            self.full_redraw = True
        elif key == Key.F6:
            self.replace_text()
            self.full_redraw = True
        elif key == Key.CTRL_N:
            self.new_document()
            self.full_redraw = True
        elif key == Key.CTRL_O:
            self.open_document()
            self.full_redraw = True
        elif key == Key.CTRL_Q or key == Key.ESCAPE:
            self.execute_command("exit")
        elif key == Key.LEFT:
            self.buffer.move_left()
        elif key == Key.RIGHT:
            self.buffer.move_right()
        elif key == Key.UP:
            self.buffer.move_up()
        elif key == Key.DOWN:
            self.buffer.move_down()
        elif key == Key.PAGE_UP:
            self.buffer.page_up(self.visible_rows())
            self.full_redraw = True
        elif key == Key.PAGE_DOWN:
            self.buffer.page_down(self.visible_rows())
            self.full_redraw = True
        elif key == Key.HOME:
            self.buffer.move_home()
        elif key == Key.END:
            self.buffer.move_end()
        elif key == Key.INSERT:
            self.insert_mode = not self.insert_mode
            self.message = "Insert mode" if self.insert_mode else "Overwrite mode"
        elif key == Key.DELETE:
            if self.buffer.column == len(self.buffer.current_line()):
                self.full_redraw = True
            self.buffer.delete()
        elif key == Key.BACKSPACE:
            if self.buffer.column == 0:
                self.full_redraw = True
            self.buffer.backspace()
        elif key == Key.ENTER:
            self.buffer.newline()
            self.full_redraw = True
        elif key == Key.TAB:
            self.buffer.insert_tab(4, self.insert_mode)
        elif key is not None and key >= 32 and key <= 126:
            self.buffer.insert(chr(key), self.insert_mode)
        self.ensure_visible()

    def process_event(self):
        previous_row = self.buffer.row
        previous_top = self.top_row
        previous_left = self.left_column
        self.handle_key(wait_key())
        if self.running:
            if (self.full_redraw or self.top_row != previous_top
                    or self.left_column != previous_left):
                self.draw()
            else:
                self.draw_incremental(previous_row)

    def run(self):
        with self.screen:
            self.draw()
            while self.running:
                self.process_event()


def run_editor(path):
    editor = Editor(path)
    editor.run()
