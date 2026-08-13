"""File chooser dialog: pattern-driven listing plus a free-typed name.

    path = app.run_modal(FileDialog("Open", "*.TXT"))

``result`` is the chosen 8.3 name or None.  The directory reader is
injectable: golden tests pass ``lister=lambda pattern: [...]`` and stay
deterministic; the default reads the DOS directory via the runtime's
FindFirst/FindNext primitives.
"""

from pydos.tui.layout import HBox, VBox
from pydos.tui.widgets.dialog import Dialog
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.button import Button
from pydos.tui.widgets.textinp import TextInput
from pydos.tui.widgets.listview import ListView


def list_directory(pattern: str) -> list:
    """All names matching the DOS pattern, sorted for determinism."""
    names: list = []
    name: str = _pydos_dir_first(pattern)
    while len(name) > 0:
        names.append(name)
        name = _pydos_dir_next()
    names.sort()
    return names


class FileDialog(Dialog):
    def __init__(self, title: str = "Open", pattern: str = "*.*",
                 lister=None) -> None:
        super().__init__(None, title, 46, 14)
        self.pattern = pattern
        self.lister = lister

        self.field = TextInput("", pattern, 78, None, self._accept)
        self.files = ListView([], 0, self._picked, self._chosen)
        ok = Button("OK", self._confirm)
        cancel = Button("Cancel", self._cancel)
        self.default_button = ok
        self.files.weight = 1
        button_row = HBox(ok, cancel)
        button_row.size_hint = 1
        self.add(VBox(
            Label("File name:", "dialog"),
            self.field,
            self.files,
            button_row,
        ))

    def on_mount(self) -> None:
        if self.lister is not None:
            self.files.items = self.lister(self.pattern)
        else:
            self.files.items = list_directory(self.pattern)

    def _picked(self, index: int) -> None:
        self.field.value = self.files.items[index]

    def _chosen(self, index: int) -> None:
        self.close(self.files.items[index])

    def _accept(self, value: str) -> None:
        self._confirm()

    def _confirm(self) -> None:
        if len(self.field.value) > 0:
            self.close(self.field.value)
        elif (len(self.files.items) > 0
              and self.files.selected >= 0):
            self.close(self.files.items[self.files.selected])
        else:
            self.close(None)

    def _cancel(self) -> None:
        self.close(None)
