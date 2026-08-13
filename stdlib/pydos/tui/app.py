"""The application: event loop, focus, bindings, modals and overlays.

    class Editor(App):
        title = "EDIT"
        bindings = {"ctrl+s": "save", "f3": "find", "escape": "quit"}

        def build(self) -> Widget:
            self.area = TextArea(self.document, gutter=True)
            return VBox(MenuBar(MENUS), Frame(self.area), StatusBar())

        def save(self) -> None: ...

    Editor().run()

One pump serves both application kinds: with ``tick_ms`` zero the loop
sleeps while idle (forms); set ``tick_ms`` and ``on_tick`` fires on a
fixed cadence (games).  Rendering recomposes the whole tree into one
Buffer only when something invalidated it, and the C present() writes
only the cells that changed.

Key routing: grab widget (open menu) -> focused widget, bubbling up
through its parents -> Tab / Shift+Tab -> class ``bindings`` -> hotkeys
registered by widgets (``app.add_hotkey``).  Modal dialogs get their own
focus ring and confine routing while open (``run_modal``).
"""

from pydos.tui.geometry import Rect
from pydos.tui.buffer import Buffer
from pydos.tui.screen import Screen
from pydos.tui.theme import Theme
from pydos.tui.widget import Widget
from pydos.tui.focus import FocusRing
from pydos.tui.keys import Key, key_from_spec
from pydos.tui.events import EventType, KeyEvent, MouseEvent
from pydos.tui.clock import ticks_ms, sleep_ms
from pydos.tui.dosinput import DosInput


class App:
    title = ""
    bindings = {}
    tick_ms = 0

    def __init__(self, screen=None, input=None, theme=None) -> None:
        self.screen = screen if screen is not None else Screen()
        self.input = input if input is not None else DosInput()
        self.theme = theme if theme is not None else Theme.turbo()
        self.root = None
        self.focus = None
        self.buffer = None
        self.running = False
        self.needs_render = True
        self.modals = []          # (widget, FocusRing) pares
        self.overlays = []
        self.key_grab = None
        self.hotkeys = []         # (Key, callback) registrados por widgets
        self.parsed_bindings = []
        self.last_tick = 0

    # -- hooks -------------------------------------------------------- #

    def build(self) -> Widget:
        return Widget()

    def on_tick(self) -> None:
        pass

    def after_event(self) -> None:
        pass

    # -- services ----------------------------------------------------- #

    def invalidate(self) -> None:
        self.needs_render = True

    def quit(self) -> None:
        self.running = False

    def action(self, name: str) -> bool:
        method = getattr(self, name, None)
        if method is None:
            return False
        method()
        return True

    def add_hotkey(self, spec: str, callback) -> None:
        self.hotkeys.append((key_from_spec(spec), callback))

    def add_overlay(self, widget: Widget) -> None:
        widget.mount(self, self.theme)
        self.overlays.append(widget)
        self.invalidate()

    def remove_overlay(self, widget: Widget) -> None:
        i: int = 0
        while i < len(self.overlays):
            if self.overlays[i] is widget:
                self.overlays.pop(i)
                self.invalidate()
                return
            i += 1

    def set_key_grab(self, widget) -> None:
        self.key_grab = widget

    def release_key_grab(self) -> None:
        self.key_grab = None

    # -- lifecycle ---------------------------------------------------- #

    def _mount(self) -> None:
        self.root = self.build()
        self.root.rect = Rect(0, 0, self.screen.width, self.screen.height)
        self.root.mount(self, self.theme)
        self.root.layout()
        self.focus = FocusRing(self.root)
        self.parsed_bindings = []
        for spec in self.bindings:
            self.parsed_bindings.append(
                (key_from_spec(spec), self.bindings[spec])
            )
        self.buffer = Buffer(self.screen.width, self.screen.height)

    def run(self) -> None:
        self._mount()
        self.running = True
        self.needs_render = True
        self.last_tick = ticks_ms()
        with self.screen:
            while self.running:
                self._pump()

    def run_modal(self, dialog):
        """Nested loop for a modal widget; returns ``dialog.result``."""
        dialog.open_flag = True
        dialog.result = None
        dialog.mount(self, self.theme)
        dialog.place(self.screen.width, self.screen.height)
        dialog.layout()
        self.modals.append((dialog, FocusRing(dialog)))
        self.invalidate()
        while self.running and dialog.open_flag:
            self._pump()
        self.modals.pop()
        self.invalidate()
        return dialog.result

    # -- pump --------------------------------------------------------- #

    def _active_ring(self):
        if len(self.modals) > 0:
            return self.modals[len(self.modals) - 1][1]
        return self.focus

    def _render(self) -> None:
        self.buffer.clear(self.theme.style("desktop"))
        self.root.render(self.buffer)
        i: int = 0
        while i < len(self.overlays):
            self.overlays[i].render(self.buffer)
            i += 1
        i = 0
        while i < len(self.modals):
            modal = self.modals[i][0]
            self.buffer.shade(Rect(modal.rect.x + 2, modal.rect.y + 1,
                                   modal.rect.width, modal.rect.height))
            modal.render(self.buffer)
            i += 1
        self.screen.present(self.buffer)
        self._place_cursor()
        self.needs_render = False

    def _place_cursor(self) -> None:
        ring = self._active_ring()
        current = ring.current() if ring is not None else None
        if current is not None:
            position: tuple = current.cursor_pos()
            if position[0] >= 0:
                self.screen.cursor(position[0], position[1], position[2])
                return
        self.screen.hide_cursor()

    def _pump(self) -> None:
        if self.needs_render:
            self._render()
        event = self.input.poll()
        if event is None:
            if self.input.closed:
                self.running = False
            elif self.tick_ms > 0:
                self._tick_pace()
            elif self.input.realtime:
                sleep_ms(10)
        elif isinstance(event, KeyEvent):
            self._dispatch_key(event)
        elif isinstance(event, MouseEvent):
            self._dispatch_mouse(event)
        self.after_event()

    def _tick_pace(self) -> None:
        now: int = ticks_ms()
        elapsed: int = now - self.last_tick
        if elapsed < 0:
            elapsed = self.tick_ms
        if elapsed >= self.tick_ms:
            self.last_tick = now
            self.on_tick()
        else:
            sleep_ms(self.tick_ms - elapsed)

    # -- routing ------------------------------------------------------ #

    def _dispatch_key(self, event) -> None:
        if self.key_grab is not None:
            if self.key_grab.on_key(event):
                return
        ring = self._active_ring()
        widget = ring.current() if ring is not None else None
        while widget is not None:
            if widget.on_key(event):
                return
            widget = widget.parent
        if event.key == Key.TAB:
            if ring is not None:
                if event.key.shift:
                    ring.focus_prev()
                else:
                    ring.focus_next()
                self.invalidate()
            return
        i: int = 0
        while i < len(self.parsed_bindings):
            pair: tuple = self.parsed_bindings[i]
            if event.key == pair[0]:
                self.action(pair[1])
                return
            i += 1
        i = 0
        while i < len(self.hotkeys):
            hot: tuple = self.hotkeys[i]
            if event.key == hot[0]:
                hot[1]()
                return
            i += 1

    def _dispatch_mouse(self, event) -> None:
        ring = self._active_ring()
        if len(self.modals) > 0:
            scope = self.modals[len(self.modals) - 1][0]
            target = scope.hit_test(event.x, event.y)
        else:
            target = None
            i: int = len(self.overlays) - 1
            while i >= 0 and target is None:
                target = self.overlays[i].hit_test(event.x, event.y)
                i -= 1
            if target is None:
                target = self.root.hit_test(event.x, event.y)
        if target is None:
            return
        if (event.kind == EventType.MOUSE_DOWN and target.focusable
                and ring is not None):
            ring.focus(target)
            self.invalidate()
        widget = target
        while widget is not None:
            if widget.on_mouse(event):
                return
            widget = widget.parent
