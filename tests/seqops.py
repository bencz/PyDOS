"""Sequence repetition and concatenation across types, scopes and counts.

A statically typed operand used to select the pydos_int_* helpers, so "ab" * 3
compiled into an integer multiply and produced a non-string.  Repetition by 2
is hidden by the strength reduction that rewrites x * 2 into x + x, so the
counts below span 0, 1, 2, 3 and larger.
"""


def repeat_local() -> None:
    text: str = "ab"
    count: int = 3
    print("local", text * count, text * 3, "ab" * count, "ab" * 3)


def repeat_untyped():
    text = "ab"
    count = 3
    print("untyped", text * count, text * 3)


class Painter:
    def __init__(self, fill: str, width: int) -> None:
        self.fill = fill
        self.width = width

    def rule(self) -> str:
        return self.fill[0] * self.width

    def typed_rule(self) -> str:
        fill: str = self.fill
        return fill * 4

    @staticmethod
    def bar(char: str, size: int) -> str:
        return char * size

    @classmethod
    def named(cls, size: int) -> str:
        return cls.bar("#", size)


repeat_local()
repeat_untyped()

painter = Painter("-", 6)
print("attr", painter.rule(), len(painter.rule()))
print("typed", painter.typed_rule())
print("static", Painter.bar("*", 5))
print("class", Painter.named(3))

module_text: str = "xy"
print("module", module_text * 4, "z" * 4)

for count in [0, 1, 2, 3, 7]:
    value = "ab" * count
    print("count", count, len(value), value)

print("negative", len("ab" * -1), len("ab" * -5))
print("empty", len("" * 5))
print("mirror", 3 * "ab", 1 * "ab", 0 * "ab")

for code in [0, 7, 32, 65, 176, 218, 255]:
    cell = chr(code) * 4
    print("chr", code, len(cell), ord(cell[0]), ord(cell[3]))

plane = chr(7) * 8
painted = plane[:1] + chr(79) * 2 + plane[3:]
codes = []
for ch in painted:
    codes.append(ord(ch))
print("plane", len(painted), codes)

numbers: list = [1, 2]
print("list", numbers * 3, 2 * numbers, len(numbers * 0))
print("listcat", numbers + [3], [0] + numbers)

pair: tuple = (1, 2)
print("tuple", pair * 3, 2 * pair, len(pair * 0))
print("tuplecat", pair + (3, 4))

raw: bytes = bytes([65, 66])
mutable: bytearray = bytearray([67, 68])
print("bytes", len(raw * 3), len(mutable * 2))

nested: list = [[0] * 3, [1] * 2]
print("nested", nested, len(nested[0]))

grid = []
row = 0
while row < 3:
    grid.append("." * 4)
    row += 1
print("grid", grid)

border: str = "+" + "-" * 6 + "+"
print("border", border, len(border))
print("expr", "ab" * (1 + 2), "ab" * len("xyz"))
