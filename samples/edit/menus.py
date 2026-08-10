"""Menu state and navigation for the EDIT sample."""

from pydos.io.tui import Key


class MenuItem:
    def __init__(self, label, shortcut, command):
        self.label = label
        self.shortcut = shortcut
        self.command = command

    def display_text(self, width):
        room = width - len(self.shortcut) - 3
        label = self.label
        if len(label) > room:
            label = label[:room]
        return " " + label.ljust(room) + " " + self.shortcut + " "


class Menu:
    def __init__(self, title, x, width, items):
        self.title = title
        self.x = x
        self.width = width
        self.items = items


class MenuBar:
    def __init__(self):
        self.menus = [
            Menu("File", 1, 24, [
                MenuItem("New", "Ctrl+N", "new"),
                MenuItem("Open...", "Ctrl+O", "open"),
                MenuItem("Save", "F2", "save"),
                MenuItem("Save as...", "", "save_as"),
                MenuItem("Exit", "Ctrl+Q", "exit"),
            ]),
            Menu("Edit", 7, 25, [
                MenuItem("Insert/overwrite", "Ins", "insert_mode"),
                MenuItem("Delete character", "Del", "delete"),
                MenuItem("Insert new line", "Enter", "newline"),
            ]),
            Menu("Search", 13, 25, [
                MenuItem("Find...", "F3", "find"),
                MenuItem("Find next", "F4", "find_next"),
                MenuItem("Replace...", "F6", "replace"),
                MenuItem("Go to line...", "F5", "goto"),
                MenuItem("Match case", "", "match_case"),
            ]),
            Menu("View", 21, 24, [
                MenuItem("Line numbers", "", "line_numbers"),
                MenuItem("Center cursor", "", "center_cursor"),
            ]),
            Menu("Help", 27, 23, [
                MenuItem("Editor help", "F1", "help"),
                MenuItem("About PyDOS", "", "about"),
            ]),
        ]
        self.menu_index = 0
        self.item_index = 0
        self.active = False

    def open(self, menu_index=0):
        self.menu_index = menu_index
        self.item_index = 0
        self.active = True

    def close(self):
        self.active = False

    def current_menu(self):
        return self.menus[self.menu_index]

    def move_menu(self, amount):
        self.menu_index += amount
        if self.menu_index < 0:
            self.menu_index = len(self.menus) - 1
        if self.menu_index >= len(self.menus):
            self.menu_index = 0
        self.item_index = 0

    def move_item(self, amount):
        menu = self.current_menu()
        self.item_index += amount
        if self.item_index < 0:
            self.item_index = len(menu.items) - 1
        if self.item_index >= len(menu.items):
            self.item_index = 0

    def handle_key(self, key):
        if key == Key.ESCAPE or key == Key.F10:
            self.close()
        elif key == Key.LEFT:
            self.move_menu(-1)
        elif key == Key.RIGHT:
            self.move_menu(1)
        elif key == Key.UP:
            self.move_item(-1)
        elif key == Key.DOWN:
            self.move_item(1)
        elif key == Key.ENTER:
            command = self.current_menu().items[self.item_index].command
            self.close()
            return command
        return None
