"""Widget collection, focus management and event loop.

The physical filename is the DOS 8.3 alias for widgets.application.
"""

from pydos.io.tui.canvas import Canvas
from pydos.io.tui.constants import Color, Key
from pydos.io.tui.keyboard import wait_key
from pydos.io.tui.screen import Screen
from pydos.io.tui.widgets.button import Button
from pydos.io.tui.widgets.label import Label


class Application:
    def __init__(self, title="PyDOS application"):
        self.title = title
        self.screen = Screen(Color.WHITE, Color.BLUE)
        self.widgets = []
        self.focusable = []
        self.focus_index = 0
        self.running = True
        self.message = "Tab changes focus; Enter activates; Esc exits"

    def add(self, widget):
        self.widgets.append(widget)
        if isinstance(widget, Button):
            self.focusable.append(widget)
        return widget

    def create_button(self, x, y, width, text, on_click, hotkey=None):
        return self.add(Button(x, y, width, text, on_click, hotkey))

    def create_label(self, x, y, text, width=0):
        return self.add(Label(x, y, text, width))

    def set_message(self, message):
        self.message = message

    def stop(self):
        self.running = False

    def focus_next(self):
        if len(self.focusable) > 0:
            self.focus_index = (self.focus_index + 1) % len(self.focusable)

    def focused_widget(self):
        if len(self.focusable) == 0:
            return None
        return self.focusable[self.focus_index]

    def dispatch_key(self, key):
        if key == Key.ESCAPE:
            self.stop()
            return
        if key == Key.TAB:
            self.focus_next()
            return
        focused = self.focused_widget()
        if focused is not None:
            focused.handle_key(key)

    def draw(self):
        canvas = Canvas(self.screen.width, self.screen.height, " ")
        canvas.draw_box(0, 0, self.screen.width, self.screen.height,
                        self.title)
        focused = self.focused_widget()
        for widget in self.widgets:
            widget.draw(canvas, widget is focused)
        canvas.draw_text(2, self.screen.height - 2,
                         self.message[:self.screen.width - 4])
        self.screen.present(canvas, Color.WHITE, Color.BLUE)

    def run(self):
        with self.screen:
            while self.running:
                self.draw()
                self.dispatch_key(wait_key())
