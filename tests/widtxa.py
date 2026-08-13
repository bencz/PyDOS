"""Golden test: the TextArea viewport widget (gutter, highlight, keys).

The document model is covered separately in widdoc.py; this program
links the full widget stack and is the largest of the widget tests.
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.theme import Theme
from pydos.tui.events import EventType, KeyEvent, MouseEvent
from pydos.tui.keys import key_from_spec
from pydos.tui.widgets.textarea import TextArea, TextDocument

theme = Theme({
    "default": Style(Color.LIGHT_GRAY, Color.BLACK),
    "textarea": Style(Color.YELLOW, Color.BLUE),
    "textarea.current": Style(Color.WHITE, Color.BLUE),
    "textarea.gutter": Style(Color.CYAN, Color.BLUE),
    "textarea.match": Style(Color.BLACK, Color.YELLOW),
    "scrollbar": Style(Color.CYAN, Color.BLUE),
})

body = TextDocument()
body.set_text("one\ntwo\nthree\nfour\nfive\nsix\nseven")

area = TextArea(body, True)
area.rect = Rect(0, 0, 14, 4)
area.theme = theme
area.scroll_glyphs = "^v.#"
area.focused = True


def press(spec: str) -> bool:
    return area.on_key(KeyEvent(key_from_spec(spec)))


buffer = Buffer(14, 4)
area.render(buffer)
print(buffer)
print(area.cursor_pos())
print("--")

press("down")
press("down")
press("down")
press("down")
press("down")
buffer = Buffer(14, 4)
area.render(buffer)
print(buffer)
print(area.top_row, area.cursor_pos())
print("--")

press("end")
press("x")
print(body.current_line())
print(press("f3"), press("ctrl+s"))

# Search highlight paints the match under the cursor
area.highlight_text = "six"
body.row = 5
body.column = 0
buffer = Buffer(14, 4)
area.render(buffer)
print(buffer)
print("--")
print(buffer.attr_map())

# Mouse click moves the cursor inside the text region
area.on_mouse(MouseEvent(EventType.MOUSE_UP, 8, 1, 0))
print(body.row, body.column)
