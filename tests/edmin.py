"""Minimal App + read_text on a headless screen.

Reproduces the editapp path (App framework plus a file read at
construction) small enough to assemble without --split, isolating
whether the editapp DOS failure is the App-on-DOS path or the large
split build.
"""

from pydos.tui.app import App
from pydos.tui.layout import VBox
from pydos.tui.widgets.label import Label
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.io.files import read_text


class Mini(App):
    def __init__(self, path, screen, input):
        super().__init__(screen, input)
        try:
            self.text = read_text(path)
        except FileNotFoundError:
            self.text = "(missing)"

    def build(self):
        return VBox(Label(self.text))


app = Mini("EDMIN312.TMP", HeadlessScreen(20, 3),
           ScriptedInput(["escape"]))
print(app.text)
app.run()
print(app.screen.presents > 0)
print(app.screen.buffer)
