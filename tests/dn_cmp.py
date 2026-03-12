# dn_cmp.py - Dunder comparison tests
# Tests: __eq__, __lt__, __gt__

class Box:
    def __init__(self, val: int) -> None:
        self.val = val

    def __eq__(self, other: Box) -> bool:
        return self.val == other.val

    def __lt__(self, other: Box) -> bool:
        return self.val < other.val

    def __gt__(self, other: Box) -> bool:
        return self.val > other.val

    def __str__(self) -> str:
        return "Box(" + str(self.val) + ")"

a: Box = Box(5)
b: Box = Box(5)
c: Box = Box(10)

print(a == b)
print(a == c)
print(a < c)
print(c > a)
