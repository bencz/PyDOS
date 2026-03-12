# dn_unar.py - Dunder unary operator tests
# Tests: __neg__, __pos__, __abs__, __invert__
class Num:
    def __init__(self, val: int) -> None:
        self.val = val
    def __neg__(self) -> Num:
        return Num(-self.val)
    def __pos__(self) -> Num:
        return Num(abs(self.val))
    def __abs__(self) -> Num:
        if self.val < 0:
            return Num(-self.val)
        return Num(self.val)
    def __invert__(self) -> Num:
        return Num(-self.val - 1)
    def __str__(self) -> str:
        return "Num(" + str(self.val) + ")"

n: Num = Num(5)
print(-n)
print(+n)
print(abs(n))
print(~n)

m: Num = Num(-3)
print(-m)
print(abs(m))
print(~m)
