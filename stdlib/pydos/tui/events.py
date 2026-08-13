"""Input events delivered by the input sources to the application."""

from dataclasses import dataclass

from pydos.tui.keys import Key


class EventType:
    KEY = 0
    MOUSE_DOWN = 1
    MOUSE_UP = 2
    MOUSE_MOVE = 3


@dataclass
class KeyEvent:
    key: Key


@dataclass
class MouseEvent:
    kind: int
    x: int
    y: int
    button: int
