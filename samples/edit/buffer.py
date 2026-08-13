"""File-backed text document for the EDIT sample.

All editing behavior lives in pydos.tui's TextDocument; this subclass
only adds DOS file persistence.  tests/editmodel.py drives it headless.
"""

from pydos.io.files import read_text, write_text
from pydos.tui.widgets.textarea import TextDocument


class TextBuffer(TextDocument):
    def __init__(self, path):
        super().__init__()
        self.path = path
        self.exists = False
        self.load(path)

    def load(self, path):
        self.path = path
        try:
            content = read_text(path)
            self.set_text(content)
            self.exists = True
        except FileNotFoundError:
            self.set_text("")
            self.exists = False

    def new_document(self, path="UNTITLED.TXT"):
        self.path = path
        self.set_text("")
        self.exists = False

    def save(self):
        write_text(self.path, "\r\n".join(self.lines) + "\r\n")
        self.dirty = False
        self.exists = True

    def save_as(self, path):
        self.path = path
        self.save()
