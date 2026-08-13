"""Golden test: MenuBar navigation and ContextMenu, fully scripted."""

from pydos.tui.app import App
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.tui.layout import VBox
from pydos.tui.widgets.menubar import MenuBar
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.statusbr import StatusBar
from pydos.tui.widgets.ctxmenu import ContextMenu

log = []

MENUS = [
    ("File", [
        ("New", "Ctrl+N", "new_file"),
        ("-", "", ""),
        ("Exit", "Ctrl+Q", "quit_app"),
    ]),
    ("View", [
        ("Numbers", "", "toggle_numbers"),
    ]),
]


class MenuApp(App):
    bindings = {"f5": "popup"}

    def build(self):
        self.menubar = MenuBar(MENUS)
        return VBox(self.menubar, Label("body"), StatusBar("st", ""))

    def new_file(self):
        log.append("new")

    def toggle_numbers(self):
        log.append("numbers")
        self.menubar.set_checked("toggle_numbers", True)

    def quit_app(self):
        log.append("exit")
        self.quit()

    def popup(self):
        choice = self.run_modal(ContextMenu(
            [("Cut", "do_cut"), ("Paste", "do_paste")], 5, 3))
        log.append("ctx:" + str(choice))


app = MenuApp(HeadlessScreen(34, 10), ScriptedInput([
    "f10", "enter",                    # File -> New
    "alt+v", "enter",                  # View -> Numbers (fica marcado)
    "f10", "right", "escape",          # navega e cancela
    "f5", "down", "enter",             # ContextMenu -> Paste
    "f10", "down", "enter",            # File: New -> (skip sep) -> Exit
]))
app.run()

print(log)
print(app.menubar.is_checked("toggle_numbers"),
      app.menubar.is_checked("new_file"))
print(app.running, app.menubar.active)
