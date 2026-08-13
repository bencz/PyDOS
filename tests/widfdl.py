"""Golden test: FileDialog with an injected deterministic lister."""

from pydos.tui.app import App
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.tui.layout import VBox
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.filedlg import FileDialog

log = []


def fake_lister(pattern):
    log.append("list:" + pattern)
    return ["ALPHA.TXT", "BRAVO.TXT", "NOTES.TXT"]


class FileApp(App):
    bindings = {"f1": "choose", "f2": "cancel_choose"}

    def build(self):
        return VBox(Label("file host"))

    def choose(self):
        picked = self.run_modal(FileDialog("Open", "*.TXT", fake_lister))
        log.append("got:" + str(picked))

    def cancel_choose(self):
        picked = self.run_modal(FileDialog("Open", "*.TXT", fake_lister))
        log.append("cancel:" + str(picked))


app = FileApp(HeadlessScreen(52, 16), ScriptedInput([
    "f1", "tab", "down", "enter",     # foca a lista, escolhe BRAVO.TXT
    "f2", "escape",                   # cancela -> None
]))
app.run()

print(log)
