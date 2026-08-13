"""Golden test for pydos.tui.Buffer: two planes, clipping, blit, shade.

Uses Border.ASCII because CP437 bytes do not survive the golden-output
comparison (CPython re-encodes them and terminals translate them).
"""

from pydos.tui import Buffer, Color, Rect, Style
from pydos.tui.glyphs import Border

buffer = Buffer(16, 6)
base = Style(Color.LIGHT_GRAY, Color.BLACK)
accent = Style(Color.WHITE, Color.BLUE)

buffer.box(Rect(0, 0, 16, 6), base, "Py", Border.ASCII)
buffer.text(2, 2, "hello", accent)
buffer.text(-3, 3, "ABCDE", base)
buffer.text(13, 4, "WXYZ", base)
with buffer.clip(Rect(4, 1, 6, 3)):
    buffer.fill(Rect(0, 0, 16, 6), "#", base)
    buffer.text(0, 2, "0123456789", accent)
print(buffer)
print("--")
print(buffer.attr_map())
print("--")

pane = Buffer(4, 2)
pane.fill(Rect(0, 0, 4, 2), "*", Style(Color.YELLOW, Color.RED))
buffer.blit(pane, 14, 4)
buffer.shade(Rect(0, 5, 4, 1))
print(buffer)
print("--")
print(buffer.attr_map())
print("--")
print(buffer.cell(2, 2), buffer.cell_attr(2, 2))
print(buffer.cell(99, 0), buffer.cell_attr(99, 0))
