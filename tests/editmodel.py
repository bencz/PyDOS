"""Headless test of the EDIT sample's document model (TextBuffer).

TextBuffer is pydos.tui's TextDocument plus DOS file persistence; the
menu behavior that used to live here moved to the library and is
covered by tests/widmnu.py.
"""

from buffer import TextBuffer
from pydos.io.files import write_text


write_text("EDT312.TMP", "")
buffer = TextBuffer("EDT312.TMP")
buffer.insert("a")
buffer.insert("b")
buffer.insert("c")
buffer.move_left()
buffer.insert("X")
print(buffer.current_line())
print(buffer.row, buffer.column)

buffer.newline()
buffer.insert("tail")
print(buffer.lines)
buffer.move_home()
buffer.backspace()
print(buffer.lines)
print(buffer.row, buffer.column)

buffer.save()
print(buffer.dirty)
loaded = TextBuffer("EDT312.TMP")
print(loaded.lines)

buffer.new_document()
buffer.insert("One fish")
buffer.newline()
buffer.insert("Two FISH")
buffer.newline()
buffer.insert("Red bird")
buffer.go_to_line(1)
buffer.move_home()
print(buffer.find("fish"), buffer.row, buffer.column)
print(buffer.find("fish", False, True), buffer.row, buffer.column)
print(buffer.replace_at_cursor("fish", "cat"), buffer.current_line())
buffer.go_to_line(99)
print(buffer.row, buffer.column)
buffer.page_up(2)
print(buffer.row, buffer.column)

buffer.new_document()
buffer.insert("abc")
buffer.move_home()
buffer.insert("Z", False)
buffer.insert_tab(4)
print(buffer.current_line(), buffer.column, buffer.dirty)
print(buffer.path, buffer.exists)
