# dn_iadd.py - Inplace dunder tests (IR_INPLACE)
# Tests: __iadd__, __isub__, __imul__

class Accum:
    def __init__(self, val: int) -> None:
        self.val = val

    def __iadd__(self, other: Accum) -> Accum:
        self.val = self.val + other.val
        return self

    def __isub__(self, other: Accum) -> Accum:
        self.val = self.val - other.val
        return self

    def __imul__(self, other: Accum) -> Accum:
        self.val = self.val * other.val
        return self

    def __str__(self) -> str:
        return "Accum(" + str(self.val) + ")"

a: Accum = Accum(10)
b: Accum = Accum(3)

# Test __iadd__
a += b
print(a)

# Test __isub__
a -= b
print(a)

# Test __imul__
a *= b
print(a)

# Verify chaining
c: Accum = Accum(5)
c += Accum(2)
c *= Accum(3)
c -= Accum(1)
print(c)
