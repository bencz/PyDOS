"""Golden test: the whole EDIT application scripted on a headless screen.

Typing, saving through the Ctrl+S binding, the Find dialog and quitting
through the unsaved-changes flow all run through the real widget tree.
"""

from editor import Editor
from pydos.tui.headless import HeadlessScreen
from pydos.tui.input import ScriptedInput
from pydos.io.files import read_text

app = Editor("EDAP312.TMP", HeadlessScreen(50, 14), ScriptedInput([
    "h", "e", "l", "l", "o", "enter",
    "w", "o", "r", "l", "d",
    "ctrl+s",                          # salva
    "f3", "w", "o", "enter",           # Find -> localiza "wo"
    "x",                               # edita de novo (dirty)
    "ctrl+q", "escape",                # sair: dialogo aparece, Esc cancela
    "ctrl+q", "tab", "enter",          # sair: botao Discard
]))
app.run()

print(app.doc.lines)
print(app.doc.row, app.doc.column)
print(app.doc.dirty, app.running)
print(read_text("EDAP312.TMP"))
print("--")
print(app.screen.buffer)
