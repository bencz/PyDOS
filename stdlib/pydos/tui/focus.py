"""Tab-order management over a widget tree.

The ring collects focusable, visible, enabled widgets in depth-first
order.  Rebuild it after anything that changes that set (adding widgets,
toggling visibility); the current focus survives a rebuild when its
widget is still eligible.
"""

from pydos.tui.widget import Widget


class FocusRing:
    def __init__(self, root: Widget) -> None:
        self.root = root
        self.order = []
        self.index = -1
        self.rebuild()

    def _collect(self, widget: Widget) -> None:
        if not widget.visible:
            return
        if widget.focusable and widget.enabled:
            self.order.append(widget)
        i: int = 0
        while i < len(widget.children):
            self._collect(widget.children[i])
            i += 1

    def _position_of(self, widget) -> int:
        i: int = 0
        while i < len(self.order):
            if self.order[i] is widget:
                return i
            i += 1
        return -1

    def rebuild(self) -> None:
        previous = self.current()
        self.order = []
        self._collect(self.root)
        self.index = self._position_of(previous)
        if self.index < 0 and len(self.order) > 0:
            self.index = 0
        self._apply_flags()

    def _apply_flags(self) -> None:
        current = self.current()
        i: int = 0
        while i < len(self.order):
            widget = self.order[i]
            widget.focused = widget is current
            i += 1
        if current is not None:
            current.invalidate()

    def current(self):
        if 0 <= self.index < len(self.order):
            return self.order[self.index]
        return None

    def focus_next(self):
        if len(self.order) == 0:
            return None
        self.index = (self.index + 1) % len(self.order)
        self._apply_flags()
        return self.current()

    def focus_prev(self):
        if len(self.order) == 0:
            return None
        self.index = (self.index - 1) % len(self.order)
        self._apply_flags()
        return self.current()

    def focus(self, widget) -> None:
        position: int = self._position_of(widget)
        if position >= 0:
            self.index = position
            self._apply_flags()
