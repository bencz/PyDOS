"""Golden test: label, button, checkbox, status bar.

Each widget-family test is a separate program on purpose: the 8086 has
640 KB for code plus heap, so linking the whole widget set into one
executable exhausts memory.  A small explicit Theme keeps Theme.turbo()
(and its 29 Style allocations) out of the binary.
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.theme import Theme
from pydos.tui.events import KeyEvent
from pydos.tui.keys import key_from_spec
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.button import Button
from pydos.tui.widgets.checkbox import CheckBox
from pydos.tui.widgets.statusbr import StatusBar

theme = Theme({
    "default": Style(Color.LIGHT_GRAY, Color.BLACK),
    "button": Style(Color.BLACK, Color.LIGHT_GRAY),
    "button.focus": Style(Color.WHITE, Color.GREEN),
    "status": Style(Color.BLACK, Color.CYAN),
})
events = []


def press(widget, spec: str) -> bool:
    return widget.on_key(KeyEvent(key_from_spec(spec)))


label = Label("Hello widgets")
label.rect = Rect(1, 0, 20, 1)
label.theme = theme

button = Button("Run", lambda: events.append("run"))
button.rect = Rect(1, 2, 11, 1)
button.theme = theme
button.focused = True

check = CheckBox("Sound", True, lambda value: events.append(value))
check.rect = Rect(14, 2, 12, 1)
check.theme = theme

status = StatusBar("Ready", "INS")
status.rect = Rect(0, 4, 28, 1)
status.theme = theme

buffer = Buffer(28, 5)
label.render(buffer)
button.render(buffer)
check.render(buffer)
status.render(buffer)
print(buffer)
print("--")
print(buffer.attr_map())
print("--")

print(press(button, "enter"), press(button, "space"), press(button, "x"))
print(press(check, "space"), check.checked)
print(events)

label.text = "Renamed"
status.text = "Saved"
status.right = "OVR"
button.focused = False
buffer = Buffer(28, 5)
label.render(buffer)
button.render(buffer)
status.render(buffer)
print(buffer)
