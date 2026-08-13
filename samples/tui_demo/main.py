"""PyDOS TUI showcase: the whole widget set in one application.

Menus, dialogs, a mouse, focus traversal, declarative bindings and a
ProgressBar animated by the tick loop.  Links every widget on purpose —
build it for the 386 target; the 8086 fits the leaner apps.
"""

from pydos.tui.app import App
from pydos.tui.layout import HBox, VBox
from pydos.tui.widgets.menubar import MenuBar
from pydos.tui.widgets.frame import Frame
from pydos.tui.widgets.label import Label
from pydos.tui.widgets.button import Button
from pydos.tui.widgets.checkbox import CheckBox
from pydos.tui.widgets.radiogrp import RadioGroup
from pydos.tui.widgets.textinp import TextInput
from pydos.tui.widgets.listview import ListView
from pydos.tui.widgets.table import Table
from pydos.tui.widgets.progress import ProgressBar
from pydos.tui.widgets.statusbr import StatusBar
from pydos.tui.widgets.msgbox import MessageBox
from pydos.tui.widgets.inputbox import InputBox
from pydos.tui.widgets.ctxmenu import ContextMenu
from pydos.tui.widgets.filedlg import FileDialog

MENUS = [
    ("Demo", [
        ("About", "F1", "about"),
        ("-", "", ""),
        ("Exit", "Ctrl+Q", "quit_app"),
    ]),
    ("Dialogs", [
        ("Message box", "", "show_message"),
        ("Input box", "", "ask_name"),
        ("File dialog", "", "pick_file"),
        ("Context menu", "", "context_menu"),
    ]),
]

CITIES = ["Amsterdam", "Brasilia", "Cairo", "Denver", "Espoo",
          "Fortaleza", "Geneva", "Harare", "Istanbul"]

TABLE_ROWS = [
    ["README", "1024", "text"],
    ["GAME", "80", "exe"],
    ["NOTES", "2200", "text"],
    ["DATA", "512", "bin"],
]


class Showcase(App):
    title = "PyDOS TUI widgets"
    bindings = {"ctrl+q": "quit_app", "f1": "about"}
    tick_ms = 100

    def build(self):
        self.status = StatusBar("Tab moves focus; F10 opens the menu",
                                "PyDOS")
        self.progress = ProgressBar(0)
        self.name_field = TextInput("", "type your name")
        self.menubar = MenuBar(MENUS)

        form = VBox(
            Label("Form widgets"),
            self.name_field,
            CheckBox("Enable sound", True, self.sound_changed),
            RadioGroup(["Slow", "Normal", "Fast"], 1, self.speed_changed),
            Button("Message", self.show_message),
            Button("Quit", self.quit),
        )
        data = VBox(
            Label("Data widgets"),
            ListView(CITIES, 0, self.city_picked, self.city_chosen),
            Table(["Name", "Size", "Kind"], [10, 6, 6], TABLE_ROWS),
            self.progress,
        )
        body = HBox(Frame(form, "Form"), Frame(data, "Data"),
                    weights=(1, 1))
        return VBox(self.menubar, body, self.status)

    # -- tick ---------------------------------------------------------- #

    def on_tick(self):
        if self.progress.value >= 100:
            self.progress.value = 0
        else:
            self.progress.value = self.progress.value + 5

    # -- actions ------------------------------------------------------- #

    def quit_app(self):
        self.quit()

    def about(self):
        self.run_modal(MessageBox(
            "About",
            ["PyDOS TUI widget showcase",
             "Compiled Python on DOS"],
        ))

    def show_message(self):
        choice = self.run_modal(MessageBox(
            "Message box",
            ["Every dialog runs in its own modal loop."],
            ("OK", "Cancel"),
        ))
        self.status.text = "Message box returned " + str(choice)

    def ask_name(self):
        name = self.run_modal(InputBox("Input box", "Your name:",
                                       self.name_field.value))
        if name is not None:
            self.name_field.value = name
            self.status.text = "Hello, " + name

    def pick_file(self):
        path = self.run_modal(FileDialog("Open file", "*.*"))
        if path is not None:
            self.status.text = "Picked " + path

    def context_menu(self):
        action = self.run_modal(ContextMenu(
            [("First", "ctx_first"), ("Second", "ctx_second")], 30, 8))
        if action is not None:
            self.action(action)

    def ctx_first(self):
        self.status.text = "Context: first"

    def ctx_second(self):
        self.status.text = "Context: second"

    # -- widget callbacks --------------------------------------------- #

    def sound_changed(self, enabled):
        self.status.text = "Sound " + ("on" if enabled else "off")

    def speed_changed(self, index):
        self.status.text = "Speed option " + str(index)

    def city_picked(self, index):
        self.status.text = "City: " + CITIES[index]

    def city_chosen(self, index):
        self.status.text = "Chosen: " + CITIES[index]


Showcase().run()
