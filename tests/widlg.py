"""Golden test: modal dialogs (MessageBox, InputBox) in a scripted App."""

from pydos.tui.app import App
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.tui.layout import VBox
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.msgbox import MessageBox
from pydos.tui.widgets.inputbox import InputBox

log = []


class DialogApp(App):
    bindings = {"f1": "ask", "f2": "message", "f3": "message_escape"}

    def build(self):
        return VBox(Label("dialog host"))

    def ask(self):
        log.append("ask:" + str(self.run_modal(
            InputBox("Name", "Type it:"))))

    def message(self):
        log.append("msg:" + str(self.run_modal(MessageBox(
            "Confirm", ["Go ahead?"], ("Yes", "No")))))

    def message_escape(self):
        log.append("esc:" + str(self.run_modal(MessageBox(
            "Confirm", ["Dismiss me"]))))


app = DialogApp(HeadlessScreen(50, 12), ScriptedInput([
    "f1", "j", "o", "enter",          # InputBox aceita "jo"
    "f1", "escape",                   # InputBox cancelada -> None
    "f2", "enter",                    # MessageBox botao default -> 0
    "f2", "tab", "enter",             # segundo botao -> 1
    "f3", "escape",                   # Escape -> -1
]))
app.run()

print(log)
print(len(app.modals), app.running)
