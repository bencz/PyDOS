"""Golden test: radio group, progress bar and frame.

Split from widbtn: one program per widget family keeps each 8086
executable inside the 640 KB budget (see widbtn.py).
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.glyphs import Border
from pydos.tui.theme import Theme
from pydos.tui.events import KeyEvent
from pydos.tui.keys import key_from_spec
from pydos.tui.widgets.radiogrp import RadioGroup
from pydos.tui.widgets.progress import ProgressBar
from pydos.tui.widgets.frame import Frame

theme = Theme({
    "default": Style(Color.LIGHT_GRAY, Color.BLACK),
    "frame": Style(Color.WHITE, Color.BLACK),
    "frame.title": Style(Color.YELLOW, Color.BLACK),
    "check.focus": Style(Color.WHITE, Color.BLUE),
    "progress": Style(Color.GREEN, Color.BLACK),
})
events = []


def press(widget, spec: str) -> bool:
    return widget.on_key(KeyEvent(key_from_spec(spec)))


frame = Frame(None, "Setup", Border.ASCII)
frame.rect = Rect(0, 0, 24, 7)
frame.theme = theme

radio = RadioGroup(["One", "Two", "Three"], 1,
                   lambda index: events.append(index))
radio.rect = Rect(2, 1, 12, 3)
radio.theme = theme
radio.focused = True

bar = ProgressBar(30, 100, True, "#.")
bar.rect = Rect(2, 5, 20, 1)
bar.theme = theme

buffer = Buffer(24, 7)
frame.render(buffer)
radio.render(buffer)
bar.render(buffer)
print(buffer)
print("--")

print(press(radio, "down"), radio.selected)
print(press(radio, "down"), radio.selected)
print(press(radio, "up"), radio.selected)
print(events)

bar.value = 150
print(bar.value)
bar.value = -5
print(bar.value)
bar.value = 100
frame.title = "Done"
buffer = Buffer(24, 7)
frame.render(buffer)
bar.render(buffer)
print(buffer)
