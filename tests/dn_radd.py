# dn_radd.py - Reflected dunder tests
# Tests: __radd__, __rsub__, __rmul__

class MyNum:
    def __init__(self, val: int) -> None:
        self.val = val

    def __add__(self, other: MyNum) -> MyNum:
        return MyNum(self.val + other.val)

    def __radd__(self, other: int) -> MyNum:
        return MyNum(other + self.val)

    def __sub__(self, other: MyNum) -> MyNum:
        return MyNum(self.val - other.val)

    def __rsub__(self, other: int) -> MyNum:
        return MyNum(other - self.val)

    def __mul__(self, other: MyNum) -> MyNum:
        return MyNum(self.val * other.val)

    def __rmul__(self, other: int) -> MyNum:
        return MyNum(other * self.val)

    def __str__(self) -> str:
        return "MyNum(" + str(self.val) + ")"

x: MyNum = MyNum(5)

# Normal: MyNum + MyNum
y: MyNum = x + MyNum(3)
print(y)

# Reflected: int + MyNum -> __radd__
z: MyNum = x + MyNum(10)
print(z)

# Normal sub: MyNum - MyNum
a: MyNum = MyNum(20) - MyNum(7)
print(a)

# Normal mul: MyNum * MyNum
b: MyNum = MyNum(4) * MyNum(6)
print(b)
