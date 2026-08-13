"""Golden test for the widget tree: focus ring, hit testing, defaults."""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.focus import FocusRing


class Item(Widget):
    def __init__(self, tag: str, can_focus: bool) -> None:
        super().__init__()
        self.tag = tag
        self.focusable = can_focus


root = Item("root", False)
root.rect = Rect(0, 0, 20, 10)
a = root.add(Item("a", True))
a.rect = Rect(1, 1, 5, 2)
panel = root.add(Item("panel", False))
panel.rect = Rect(0, 5, 20, 5)
b = panel.add(Item("b", True))
b.rect = Rect(2, 6, 6, 1)
c = panel.add(Item("c", False))
c.rect = Rect(2, 8, 6, 1)
d = root.add(Item("d", True))
d.rect = Rect(10, 1, 6, 2)

ring = FocusRing(root)
print(ring.current().tag, len(ring.order))
print(ring.focus_next().tag)
print(ring.focus_next().tag)
print(ring.focus_next().tag)
print(ring.focus_prev().tag)
print(a.focused, b.focused, d.focused)

# Rebuild after b disappears and d is disabled: focus falls back to a
b.visible = False
d.enabled = False
ring.rebuild()
print(ring.current().tag, len(ring.order))
print(a.focused, d.focused)

# Hit testing finds the deepest visible widget; later siblings win
print(root.hit_test(2, 1).tag)
print(root.hit_test(3, 6).tag)      # b invisible: falls through to panel
print(root.hit_test(3, 8).tag)
print(root.hit_test(11, 2).tag)
print(root.hit_test(19, 0).tag)
print(root.hit_test(50, 50))

# Defaults every widget inherits
print(a.cursor_pos())
print(a.theme_style("anything").attr())
print(a.on_key(None), a.on_mouse(None))
