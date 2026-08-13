"""Golden test: TextInput editing, scrolling, placeholder and cursor."""

from pydos.tui import Rect
from pydos.tui.theme import Theme
from pydos.tui.events import KeyEvent
from pydos.tui.keys import key_from_spec

from pydos.tui.widgets.textinp import TextInput

theme = Theme.turbo()
changes = []
submits = []

field = TextInput("abc", "type here", 10,
                  lambda value: changes.append(value),
                  lambda value: submits.append(value))
field.rect = Rect(0, 0, 6, 1)
field.theme = theme
field.focused = True


def press(spec: str) -> bool:
    return field.on_key(KeyEvent(key_from_spec(spec)))


print("|" + field.display_text() + "|", field.cursor_pos())

press("d")
press("e")
press("f")
print(field.value, "|" + field.display_text() + "|", field.cursor_pos())

press("home")
print(field.cursor_pos())
press("shift+x")
print(field.value)
press("delete")
print(field.value)
press("end")
press("backspace")
print(field.value)

# Overwrite mode
press("insert")
press("home")
press("z")
print(field.value, field.insert_mode)

# max_length blocks growth beyond 10
field.value = "0123456789"
press("end")
press("q")
print(field.value)

# Submit and unhandled keys
print(press("enter"), submits)
print(press("f5"))
print(changes)

# Placeholder shows when empty and unfocused
field.value = ""
field.focused = False
print("|" + field.display_text() + "|")
field.focused = True
print("|" + field.display_text() + "|", field.cursor_pos())
