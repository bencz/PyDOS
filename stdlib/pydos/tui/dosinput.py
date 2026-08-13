"""Live DOS input source: extended keyboard plus optional mouse.

Kept apart from pydos.tui.input on purpose: this module calls the new C
primitives (_pydos_tui_key_event, _pydos_tui_mouse_*), so it can only be
compiled once those entries exist in the stdlib index.  ScriptedInput
and the packed-state decoder stay importable everywhere.

Polling never allocates while idle: events are decoded into objects only
when something actually happened.
"""

from pydos.tui.keys import decode_key
from pydos.tui.events import KeyEvent
from pydos.tui.input import decode_mouse_packed


class DosInput:
    def __init__(self, mouse: bool = True) -> None:
        self.closed = False
        self.realtime = True
        self.mouse_available = False
        self.last_x = -1
        self.last_y = -1
        self.last_state = -1
        if mouse:
            self.mouse_available = _pydos_tui_mouse_init() > 0
            if self.mouse_available:
                _pydos_tui_mouse_show(True)

    def poll(self):
        """Next pending event, or None when the queues are idle."""
        packed: int = _pydos_tui_key_event()
        if packed >= 0:
            return KeyEvent(decode_key(packed))
        if self.mouse_available:
            state: int = _pydos_tui_mouse_poll()
            if state >= 0 and state != self.last_state:
                self.last_state = state
                event = decode_mouse_packed(state, self.last_x, self.last_y)
                if event is not None:
                    self.last_x = event.x
                    self.last_y = event.y
                    return event
        return None
