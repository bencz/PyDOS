"""Golden test for pydos.tui geometry and styles (pure Python core)."""

from pydos.tui.geometry import Rect
from pydos.tui.color import Color, Style

r = Rect(2, 3, 10, 4)
print(r)
print(r.right(), r.bottom())
print(r.contains(2, 3), r.contains(11, 6), r.contains(12, 6), r.contains(2, 7))
inner = r.inset(1)
print(inner)
print(inner.is_empty(), Rect(0, 0, 0, 5).is_empty())

a = Rect(0, 0, 6, 6)
b = Rect(4, 4, 6, 6)
print(a.intersect(b))
print(b.intersect(a))
print(a.intersect(Rect(10, 10, 2, 2)))

top, rest = r.split_top(1)
print(top)
print(rest)
main, status = r.split_bottom(2)
print(main)
print(status)
left, right = r.split_left(3)
print(left)
print(right)
print(r.split_top(99))

style = Style(Color.WHITE, Color.BLUE)
print(style.attr())
print(style.inverted().attr())
print(style.with_fg(Color.YELLOW).attr())
print(style.with_bg(Color.LIGHT_GRAY).attr())
print(Style(7, 0, True).attr())
print(Style().attr())
