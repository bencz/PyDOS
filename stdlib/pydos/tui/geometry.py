"""Rectangles for layout, clipping and hit-testing.

Coordinates are text cells; ``width``/``height`` may be zero (an empty
rectangle contains nothing and intersects nothing).
"""

from dataclasses import dataclass


@dataclass
class Rect:
    x: int = 0
    y: int = 0
    width: int = 0
    height: int = 0

    def right(self) -> int:
        """First column to the right of the rectangle."""
        return self.x + self.width

    def bottom(self) -> int:
        """First row below the rectangle."""
        return self.y + self.height

    def is_empty(self) -> bool:
        return self.width <= 0 or self.height <= 0

    def contains(self, px: int, py: int) -> bool:
        if self.is_empty():
            return False
        return (self.x <= px < self.right()) and (self.y <= py < self.bottom())

    def inset(self, amount: int) -> "Rect":
        """Shrink by ``amount`` cells on every side (grow when negative)."""
        return Rect(
            self.x + amount,
            self.y + amount,
            self.width - 2 * amount,
            self.height - 2 * amount,
        )

    def intersect(self, other: "Rect") -> "Rect":
        """Overlap of two rectangles; empty (0x0) when they do not touch."""
        left: int = self.x if self.x > other.x else other.x
        top: int = self.y if self.y > other.y else other.y
        right_edge: int = self.right() if self.right() < other.right() else other.right()
        bottom_edge: int = (
            self.bottom() if self.bottom() < other.bottom() else other.bottom()
        )
        if right_edge <= left or bottom_edge <= top:
            return Rect(left, top, 0, 0)
        return Rect(left, top, right_edge - left, bottom_edge - top)

    def split_top(self, rows: int) -> tuple:
        """(top slice of ``rows`` lines, remainder below it)."""
        if rows < 0:
            rows = 0
        if rows > self.height:
            rows = self.height
        top = Rect(self.x, self.y, self.width, rows)
        rest = Rect(self.x, self.y + rows, self.width, self.height - rows)
        return (top, rest)

    def split_bottom(self, rows: int) -> tuple:
        """(remainder above, bottom slice of ``rows`` lines)."""
        if rows < 0:
            rows = 0
        if rows > self.height:
            rows = self.height
        rest = Rect(self.x, self.y, self.width, self.height - rows)
        bottom = Rect(self.x, self.y + self.height - rows, self.width, rows)
        return (rest, bottom)

    def split_left(self, cols: int) -> tuple:
        """(left slice of ``cols`` columns, remainder to its right)."""
        if cols < 0:
            cols = 0
        if cols > self.width:
            cols = self.width
        left = Rect(self.x, self.y, cols, self.height)
        rest = Rect(self.x + cols, self.y, self.width - cols, self.height)
        return (left, rest)
