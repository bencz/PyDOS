"""Scripted input source and the shared mouse-state decoder.

``ScriptedInput`` feeds a fixed list of events to an App, which makes a
whole application deterministic and testable without a screen:

    app = Editor(screen=HeadlessScreen(80, 25),
                 input=ScriptedInput(["ctrl+o", "a", "enter", "escape"]))

Entries are key specs ("ctrl+s", "escape", "a") or mouse tuples:
("down", x, y, button), ("up", x, y, button), ("move", x, y).
When the script runs out, ``closed`` becomes True and App.run() ends.

The live DOS input source lives in pydos.tui.dosinput: it calls the C
keyboard/mouse primitives, so it can only be linked once those exist in
the stdlib index — this module stays importable everywhere.
"""

from pydos.tui.keys import key_from_spec
from pydos.tui.events import EventType, KeyEvent, MouseEvent


def decode_mouse_packed(state: int, last_x: int, last_y: int):
    """Turn a packed _pydos_tui_mouse_poll() integer into an event.

    Priority: presses beat releases beat movement, so a click is never
    lost even when the pointer moved in the same poll.  Returns None
    when nothing observable happened (caller keeps last_x/last_y).
    """
    x: int = state & 255
    y: int = (state >> 8) & 255
    if state & (1 << 19):
        return MouseEvent(EventType.MOUSE_DOWN, x, y, 0)
    if state & (1 << 21):
        return MouseEvent(EventType.MOUSE_DOWN, x, y, 1)
    if state & (1 << 20):
        return MouseEvent(EventType.MOUSE_UP, x, y, 0)
    if state & (1 << 22):
        return MouseEvent(EventType.MOUSE_UP, x, y, 1)
    if x != last_x or y != last_y:
        return MouseEvent(EventType.MOUSE_MOVE, x, y, -1)
    return None


class ScriptedInput:
    def __init__(self, script: list) -> None:
        self.script = script
        self.index = 0
        self.closed = False
        self.realtime = False

    def poll(self):
        """Next scripted event; sets ``closed`` when the script ends."""
        if self.index >= len(self.script):
            self.closed = True
            return None
        entry = self.script[self.index]
        self.index += 1
        if isinstance(entry, str):
            return KeyEvent(key_from_spec(entry))
        kind_name: str = entry[0]
        if kind_name == "move":
            return MouseEvent(EventType.MOUSE_MOVE, entry[1], entry[2], -1)
        if kind_name == "down":
            return MouseEvent(EventType.MOUSE_DOWN, entry[1], entry[2],
                              entry[3])
        return MouseEvent(EventType.MOUSE_UP, entry[1], entry[2], entry[3])
