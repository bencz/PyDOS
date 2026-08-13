"""Golden test: ListView viewport, scrollbar glyphs and mouse clicks.

Table lives in widtab.py: one widget family per program keeps each 8086
executable inside the 640 KB budget (see widbtn.py).
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.theme import Theme
from pydos.tui.events import EventType, KeyEvent, MouseEvent
from pydos.tui.keys import key_from_spec
from pydos.tui.widgets.listview import ListView

theme = Theme({
    "default": Style(Color.LIGHT_GRAY, Color.BLACK),
    "list": Style(Color.BLACK, Color.CYAN),
    "list.selected": Style(Color.WHITE, Color.GREEN),
    "scrollbar": Style(Color.CYAN, Color.BLACK),
})
picks = []
actions = []

items = ["alpha", "bravo", "charlie", "delta", "echo", "foxtrot",
         "golf", "hotel"]
view = ListView(items, 0, lambda index: picks.append(index),
                lambda index: actions.append(index))
view.rect = Rect(0, 0, 12, 4)
view.theme = theme
view.scroll_glyphs = "^v.#"


def press(spec: str) -> bool:
    return view.on_key(KeyEvent(key_from_spec(spec)))


buffer = Buffer(12, 4)
view.render(buffer)
print(buffer)
print("--")

press("down")
press("down")
press("down")
press("down")
buffer = Buffer(12, 4)
view.render(buffer)
print(buffer)
print(view.selected, view.top)
print("--")

press("end")
press("enter")
buffer = Buffer(12, 4)
view.render(buffer)
print(buffer)
print(view.selected, view.top, actions)
print("--")

press("page_up")
print(view.selected, view.top)
print(picks)

# Mouse: click selects, second click activates
view.on_mouse(MouseEvent(EventType.MOUSE_UP, 3, 1, 0))
print(view.selected)
view.on_mouse(MouseEvent(EventType.MOUSE_UP, 3, 1, 0))
print(actions)
