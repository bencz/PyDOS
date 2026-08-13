"""EDIT: the PyDOS text editor, rebuilt on pydos.tui widgets.

The 596-line hand-drawn UI became a widget tree plus a table of
actions: menus, dialogs, the status bar, scrolling, focus and redraw
all come from the library.  Rendering recomposes the tree only when
something changed, and the C engine writes only the cells that differ.

The document model lives in buffer.TextBuffer (a TextDocument with DOS
file persistence), which tests/editmodel.py checks headless; the whole
application can also run scripted — see tests/editapp.py.
"""

from pydos.tui.app import App
from pydos.tui.layout import VBox
from pydos.tui.widgets.menubar import MenuBar
from pydos.tui.widgets.frame import Frame
from pydos.tui.widgets.textarea import TextArea
from pydos.tui.widgets.statusbr import StatusBar
from pydos.tui.widgets.msgbox import MessageBox
from pydos.tui.widgets.inputbox import InputBox
from buffer import TextBuffer
from theme import editor_theme

MENUS = [
    ("File", [
        ("New", "Ctrl+N", "new_file"),
        ("Open...", "Ctrl+O", "open_file"),
        ("Save", "Ctrl+S", "save_file"),
        ("Save As...", "", "save_file_as"),
        ("-", "", ""),
        ("Exit", "Ctrl+Q", "quit_app"),
    ]),
    ("Search", [
        ("Find...", "Ctrl+F", "find_text"),
        ("Find Next", "F4", "find_next"),
        ("Replace", "F6", "replace_once"),
        ("Go To Line...", "Ctrl+G", "go_to_line"),
        ("-", "", ""),
        ("Match Case", "", "toggle_case"),
    ]),
    ("View", [
        ("Line Numbers", "", "toggle_gutter"),
    ]),
    ("Help", [
        ("About", "F1", "about"),
    ]),
]

SHORTCUTS = " F1 Help  F2 Save  F3 Find  F4 Next  F6 Replace  Ctrl+Q Exit"


class Editor(App):
    title = "EDIT"
    bindings = {
        "f1": "about",
        "f2": "save_file",
        "ctrl+s": "save_file",
        "f3": "find_text",
        "ctrl+f": "find_text",
        "f4": "find_next",
        "ctrl+g": "go_to_line",
        "f6": "replace_once",
        "ctrl+n": "new_file",
        "ctrl+o": "open_file",
        "ctrl+q": "quit_app",
    }

    def __init__(self, path="DOCUMENT.TXT", screen=None, input=None):
        super().__init__(screen, input, editor_theme())
        self.doc = TextBuffer(path)
        self.last_search = ""
        self.case_sensitive = False

    # -- tree ---------------------------------------------------------- #

    def build(self):
        self.menubar = MenuBar(MENUS)
        self.area = TextArea(self.doc, False)
        self.frame = Frame(self.area, self.title_text())
        self.status = StatusBar("", "")
        self.shortcuts = StatusBar(SHORTCUTS, "", "status.key")
        return VBox(self.menubar, self.frame, self.status, self.shortcuts)

    def title_text(self):
        marker = " *" if self.doc.dirty else ""
        return "EDIT  " + self.doc.path + marker

    def after_event(self):
        new_title = self.title_text()
        if self.frame.title != new_title:
            self.frame.title = new_title
        mode = "INS" if self.area.insert_mode else "OVR"
        line = (" Ln " + str(self.doc.row + 1) + "/"
                + str(len(self.doc.lines))
                + "  Col " + str(self.doc.column + 1) + "  " + mode)
        if self.status.text != line:
            self.status.text = line

    # -- file actions -------------------------------------------------- #

    def confirm_unsaved(self):
        """True when it is safe to discard the current document."""
        if not self.doc.dirty:
            return True
        choice = self.run_modal(MessageBox(
            "Unsaved document",
            ["Save changes to " + self.doc.path + "?"],
            ("Save", "Discard", "Cancel"),
        ))
        if choice == 0:
            self.save_file()
            return not self.doc.dirty
        return choice == 1

    def new_file(self):
        if self.confirm_unsaved():
            self.doc.new_document()
            self.invalidate()

    def open_file(self):
        if not self.confirm_unsaved():
            return
        path = self.run_modal(InputBox("Open", "File name:",
                                       self.doc.path))
        if path is not None and len(path) > 0:
            self.doc.load(path)
            self.area.top_row = 0
            self.area.left_column = 0
            self.invalidate()

    def save_file(self):
        self.doc.save()
        self.invalidate()

    def save_file_as(self):
        path = self.run_modal(InputBox("Save as", "File name:",
                                       self.doc.path))
        if path is not None and len(path) > 0:
            self.doc.save_as(path)
            self.invalidate()

    def quit_app(self):
        if self.confirm_unsaved():
            self.quit()

    # -- search actions ------------------------------------------------ #

    def find_text(self):
        query = self.run_modal(InputBox("Find", "Search for:",
                                        self.last_search))
        if query is None or len(query) == 0:
            return
        self.last_search = query
        self.area.highlight_text = query
        self.area.highlight_case = self.case_sensitive
        if not self.doc.find(query, self.case_sensitive):
            self.run_modal(MessageBox("Find",
                                      ["Text not found: " + query]))
        self.invalidate()

    def find_next(self):
        if len(self.last_search) == 0:
            self.find_text()
            return
        if not self.doc.find(self.last_search, self.case_sensitive, True):
            self.run_modal(MessageBox(
                "Find", ["No more matches: " + self.last_search]))
        self.invalidate()

    def replace_once(self):
        query = self.run_modal(InputBox("Replace", "Search for:",
                                        self.last_search))
        if query is None or len(query) == 0:
            return
        replacement = self.run_modal(InputBox("Replace", "Replace with:"))
        if replacement is None:
            return
        self.last_search = query
        if not self.doc.find(query, self.case_sensitive):
            self.run_modal(MessageBox("Replace",
                                      ["Text not found: " + query]))
        elif not self.doc.replace_at_cursor(query, replacement,
                                            self.case_sensitive):
            self.run_modal(MessageBox("Replace", ["Nothing replaced"]))
        self.invalidate()

    def go_to_line(self):
        answer = self.run_modal(InputBox("Go to line", "Line number:"))
        if answer is None or len(answer) == 0:
            return
        try:
            number = int(answer)
        except ValueError:
            self.run_modal(MessageBox("Go to line",
                                      ["Not a number: " + answer]))
            return
        self.doc.go_to_line(number)
        self.invalidate()

    # -- toggles and help ---------------------------------------------- #

    def toggle_case(self):
        self.case_sensitive = not self.case_sensitive
        self.menubar.set_checked("toggle_case", self.case_sensitive)

    def toggle_gutter(self):
        self.area.gutter = not self.area.gutter
        self.menubar.set_checked("toggle_gutter", self.area.gutter)

    def about(self):
        self.run_modal(MessageBox(
            "About EDIT",
            ["PyDOS sample editor",
             "Rebuilt on the pydos.tui widget toolkit"],
        ))


def run_editor(path="DOCUMENT.TXT"):
    Editor(path).run()
