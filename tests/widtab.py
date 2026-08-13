"""Golden test: Table columns, horizontal scrolling and thumb math.

Split from widlst.py to keep the 8086 executable inside 640 KB.
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.geometry import Rect as GRect
from pydos.tui.theme import Theme
from pydos.tui.events import KeyEvent
from pydos.tui.keys import key_from_spec
from pydos.tui.widgets.table import Table
from pydos.tui.widgets.scrollbr import ScrollBar

theme = Theme({
    "default": Style(Color.LIGHT_GRAY, Color.BLACK),
    "list": Style(Color.BLACK, Color.CYAN),
    "list.selected": Style(Color.WHITE, Color.GREEN),
    "table.header": Style(Color.WHITE, Color.BLUE),
})
picks = []
actions = []

grid = Table(
    ["Name", "Size", "Kind"],
    [8, 5, 6],
    [
        ["README", "1024", "text"],
        ["GAME", "80", "exe"],
        ["NOTES", "2200", "text"],
        ["DATA", "512", "bin"],
    ],
    0,
    lambda index: picks.append(index),
    lambda index: actions.append(index),
)
grid.rect = Rect(0, 0, 21, 4)
grid.theme = theme


def press(spec: str) -> bool:
    return grid.on_key(KeyEvent(key_from_spec(spec)))


buffer = Buffer(21, 4)
grid.render(buffer)
print(buffer)
print("--")

press("down")
press("down")
press("right")
buffer = Buffer(21, 4)
grid.render(buffer)
print(buffer)
print(grid.selected, grid.first_column)
press("enter")
print(actions)
press("left")
print(grid.first_column)
print(picks)

# ScrollBar thumb positioning math
rect = GRect(0, 0, 1, 6)
print(ScrollBar.thumb_row(rect, 20, 6, 0))
print(ScrollBar.thumb_row(rect, 20, 6, 7))
print(ScrollBar.thumb_row(rect, 20, 6, 14))
print(ScrollBar.thumb_row(rect, 5, 6, 0))
