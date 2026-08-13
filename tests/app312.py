"""Golden test: a scripted App end to end on a headless screen.

Typing, Tab focus traversal, button activation and a class binding all
run through the real event loop; the final frame is the golden output.
"""

from pydos.tui import Buffer
from pydos.tui.app import App
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.tui.layout import VBox
from pydos.tui.theme import Theme
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.textinp import TextInput
from pydos.tui.widgets.button import Button
from pydos.tui.widgets.statusbr import StatusBar

log = []


class Demo(App):
    title = "demo"
    bindings = {"ctrl+s": "save"}

    def build(self):
        self.field = TextInput("", "name here")
        self.status = StatusBar("ready", "demo")
        return VBox(
            Label("Scripted demo"),
            self.field,
            Button("OK", self.confirm),
            self.status,
        )

    def confirm(self):
        log.append("ok:" + self.field.value)
        self.status.text = "confirmed"

    def save(self):
        log.append("saved")


app = Demo(HeadlessScreen(24, 6), ScriptedInput([
    "h", "i", "shift+a",
    "tab", "enter",
    "ctrl+s",
]))
app.run()

print(log)
print(app.field.value)
print(app.screen.presents > 0)
print(app.screen.buffer)
print("--")
print(app.screen.last_cursor)
