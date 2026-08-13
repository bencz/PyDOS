"""Golden test for pydos.tui events, ScriptedInput and mouse decoding."""

from pydos.tui.events import EventType, KeyEvent, MouseEvent
from pydos.tui.input import ScriptedInput, decode_mouse_packed

print(EventType.KEY, EventType.MOUSE_DOWN, EventType.MOUSE_UP,
      EventType.MOUSE_MOVE)

source = ScriptedInput([
    "ctrl+o",
    "a",
    "enter",
    ("down", 10, 5, 0),
    ("up", 10, 5, 0),
    ("move", 3, 4),
    "escape",
])

while True:
    event = source.poll()
    if event is None:
        break
    print(event)
print(source.closed)
print(source.poll())

# Packed mouse states straight from the C layout: col | row << 8 |
# buttons << 16 | press/release flags in bits 19-22.
press = 10 | (5 << 8) | (1 << 16) | (1 << 19)
release = 10 | (5 << 8) | (1 << 20)
right_press = 70 | (20 << 8) | (2 << 16) | (1 << 21)
moved = 12 | (6 << 8)

print(decode_mouse_packed(press, -1, -1))
print(decode_mouse_packed(release, 10, 5))
print(decode_mouse_packed(right_press, 10, 5))
print(decode_mouse_packed(moved, 10, 5))
print(decode_mouse_packed(moved, 12, 6))

# A press in the same poll as a move: the click wins
both = 33 | (9 << 8) | (1 << 16) | (1 << 19)
print(decode_mouse_packed(both, 0, 0))
