"""Golden test for VBox/HBox: size hints, weights, remainders, nesting."""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.layout import HBox, VBox


class Pane(Widget):
    def __init__(self, tag: str, hint: int = 0) -> None:
        super().__init__()
        self.tag = tag
        self.size_hint = hint


# Classic app frame: fixed bars, flexible body
menu = Pane("menu", 1)
body = Pane("body")
status = Pane("status", 1)
root = VBox(menu, body, status)
root.rect = Rect(0, 0, 80, 25)
root.layout()
print(menu.rect)
print(body.rect)
print(status.rect)

# Weighted split
left = Pane("left")
right = Pane("right")
row = HBox(left, right, weights=(1, 3))
row.rect = Rect(0, 5, 80, 10)
row.layout()
print(left.rect)
print(right.rect)

# Remainder goes to the last flexible child
first = Pane("first")
second = Pane("second")
third = Pane("third")
thirds = HBox(first, second, third)
thirds.rect = Rect(0, 0, 80, 5)
thirds.layout()
print(first.rect)
print(second.rect)
print(third.rect)

# Invisible children release their space
second.visible = False
thirds.layout()
print(first.rect)
print(second.rect)
print(third.rect)

# Nesting: an HBox inside a VBox body
inner_left = Pane("inner_left")
inner_right = Pane("inner_right")
shell = VBox(Pane("top", 2), HBox(inner_left, inner_right, weights=(2, 1)))
shell.rect = Rect(0, 0, 60, 20)
shell.layout()
print(inner_left.rect)
print(inner_right.rect)
