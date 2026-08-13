"""Message dialog with a row of buttons.

    choice = app.run_modal(MessageBox(
        "Unsaved document",
        ["Save changes before closing?"],
        ("Save", "Discard", "Cancel"),
    ))

``result`` is the pressed button's index, or -1 for Escape.
"""

from pydos.tui.geometry import Rect
from pydos.tui.keys import Key
from pydos.tui.layout import HBox, VBox
from pydos.tui.widgets.dialog import Dialog
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.button import Button


class MessageBox(Dialog):
    def __init__(self, title: str, lines: list,
                 buttons: tuple = ("OK",)) -> None:
        super().__init__(None, title)
        self.lines = lines

        width: int = len(title) + 8
        i: int = 0
        while i < len(lines):
            if len(lines[i]) + 6 > width:
                width = len(lines[i]) + 6
            i += 1
        buttons_width: int = 0
        button_row = HBox()
        button_row.size_hint = 1
        i = 0
        while i < len(buttons):
            handler = self._make_close(i)
            button = Button(buttons[i], handler)
            button_row.add(button)
            buttons_width += len(buttons[i]) + 6
            if i == 0:
                self.default_button = button
            i += 1
        if buttons_width + 4 > width:
            width = buttons_width + 4
        self.want_width = width
        self.want_height = len(lines) + 5

        body = VBox()
        i = 0
        while i < len(lines):
            body.add(Label(lines[i], "dialog"))
            i += 1
        spacer = Label("", "dialog")
        body.add(spacer)
        body.add(button_row)
        self.add(body)

    def _make_close(self, index: int):
        def handler() -> None:
            self.close(index)
        return handler

    def on_key(self, event) -> bool:
        if event.key == Key.ESCAPE:
            self.close(-1)
            return True
        return super().on_key(event)
