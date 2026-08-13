"""Regression: a chained comparison inside and/or must merge correctly.

The chained-compare lowering used to return the last comparison's SSA
value, whose defining instruction never executes when an earlier link
short-circuits — harmless-looking alone, a null result at runtime once
the value flowed through the and/or alloca merge.  Every branch below
exercises a distinct path through the merged short-circuit blocks.
"""


def band(code: int) -> bool:
    return 1 == 1 and 32 <= code <= 126 and 2 == 2


def bor(code: int) -> bool:
    return 1 == 2 or 32 <= code <= 126 or 2 == 3


def triple(a: int, b: int, c: int) -> bool:
    return 0 <= a <= b <= c and c < 100


class Gate:
    def __init__(self, code: int, name: str, ctrl: bool, alt: bool) -> None:
        self.code = code
        self.name = name
        self.ctrl = ctrl
        self.alt = alt

    def is_printable(self) -> bool:
        return (
            len(self.name) == 1
            and 32 <= self.code <= 126
            and not self.ctrl
            and not self.alt
        )


print(band(19), band(97), band(200))
print(bor(19), bor(97), bor(200))
print(triple(1, 2, 3), triple(5, 2, 3), triple(1, 2, 300))
print(Gate(97, "a", False, False).is_printable())
print(Gate(19, "s", True, False).is_printable())
print(Gate(328, "up", False, False).is_printable())
