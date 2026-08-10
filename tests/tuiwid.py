from pydos.io.tui import Canvas, Dialog, Key, TextInput
from pydos.io.tui import create_button, create_label
from pydos.io.tui.widgets import Application, Button, Label, Widget


events = []
button = create_button(1, 1, 12, "Run", lambda: events.append("clicked"))
label = create_label(1, 0, "Title")
canvas = Canvas(16, 3, ".")
label.draw(canvas)
button.draw(canvas, True)
for line in canvas.to_lines():
    print(line)

print(isinstance(button, Button))
print(isinstance(button, Widget))
print(isinstance(label, Label))
print(button.handle_key(Key.ENTER))
print(events)

app = Application("Test")
app.create_button(0, 0, 10, "One", lambda: events.append("one"))
app.create_button(0, 1, 10, "Two", lambda: events.append("two"))
print(app.focus_index)
app.dispatch_key(Key.TAB)
print(app.focus_index)
app.dispatch_key(Key.ENTER)
print(events)
app.dispatch_key(Key.ESCAPE)
print(app.running)

field = TextInput(2, 0, 4, "abcdef", 8)
print("|" + field.display_text() + "|", field.cursor_x())
field.handle_key(Key.HOME)
field.handle_key(Key.DELETE)
field.handle_key(Key.INSERT)
field.handle_key(90)
field.handle_key(Key.END)
field.handle_key(Key.BACKSPACE)
print(field.value, field.cursor, field.insert_mode)

dialog_canvas = Canvas(20, 7, ".")
dialog = Dialog(1, 1, 18, 5, "Info", ["alpha", "beta"])
dialog.draw(dialog_canvas)
for line in dialog_canvas.to_lines():
    print(line)
print(Key.F6, Key.ALT_F, Key.CTRL_Q)
