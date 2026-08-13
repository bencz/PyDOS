"""One-field prompt dialog.

    name = app.run_modal(InputBox("Save as", "File name:", "DOC.TXT"))

``result`` is the accepted string, or None when cancelled (Escape or
the Cancel button).  Enter accepts from anywhere in the dialog.
"""

from pydos.tui.layout import HBox, VBox
from pydos.tui.widgets.dialog import Dialog
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.button import Button
from pydos.tui.widgets.textinp import TextInput


class InputBox(Dialog):
    def __init__(self, title: str, label: str, value: str = "",
                 max_length: int = 255) -> None:
        super().__init__(None, title)
        width: int = len(title) + 8
        if len(label) + 6 > width:
            width = len(label) + 6
        if width < 46:
            width = 46
        self.want_width = width
        self.want_height = 8

        self.field = TextInput(value, "", max_length, None, self._accept)
        ok = Button("OK", self._confirm)
        cancel = Button("Cancel", self._cancel)
        self.default_button = ok
        button_row = HBox(ok, cancel)
        button_row.size_hint = 1
        self.add(VBox(
            Label(label, "dialog"),
            self.field,
            Label("", "dialog"),
            button_row,
        ))

    def _accept(self, value: str) -> None:
        self.close(value)

    def _confirm(self) -> None:
        self.close(self.field.value)

    def _cancel(self) -> None:
        self.close(None)
