"""Golden test: the TextDocument editing model, no widget linked.

Importing only TextDocument keeps TextArea (and through dead-code
elimination the whole buffer/theme/widget stack) out of the executable,
so this covers the model on the 8086 even though the full TextArea
program only fits the 386.
"""

from pydos.tui.widgets.textarea import TextDocument

doc = TextDocument()
doc.set_text("alpha\nbravo\ncharlie")
print(len(doc.lines), doc.row, doc.column, doc.dirty)

doc.move_end()
doc.insert("!")
print(doc.current_line(), doc.dirty)

doc.newline()
doc.insert("inserted")
print(doc.text())
print("--")

doc.move_home()
doc.backspace()
print(doc.current_line(), doc.row)

doc.move_end()
doc.delete()
print(doc.current_line())

print(doc.find("char"), doc.row, doc.column)
print(doc.find("CHAR", True), doc.find("missing"))
print(doc.replace_at_cursor("char", "SUPER"), doc.current_line())

doc.go_to_line(1)
print(doc.row)
doc.page_down(2)
print(doc.row)
doc.insert_tab()
print("|" + doc.current_line() + "|")

# Overwrite mode replaces instead of inserting
doc.move_home()
doc.insert("zz", False)
print("|" + doc.current_line() + "|")

# Goal column survives passing through a short line
tall = TextDocument()
tall.set_text("longer line\nab\nanother long")
tall.move_end()
print(tall.column)
tall.move_down()
print(tall.column)
tall.move_down()
print(tall.column)

# Joining lines with backspace at column zero
join = TextDocument()
join.set_text("ab\ncd")
join.move_down()
join.move_home()
join.backspace()
print(join.text(), join.row, join.column)
