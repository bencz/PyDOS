# dn_arth.py - Dunder arithmetic tests
# Tests: __add__, __sub__, __mul__

class Vector:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

    def __add__(self, other: Vector) -> Vector:
        return Vector(self.x + other.x, self.y + other.y)

    def __sub__(self, other: Vector) -> Vector:
        return Vector(self.x - other.x, self.y - other.y)

    def __mul__(self, other: Vector) -> Vector:
        return Vector(self.x * other.x, self.y * other.y)

    def __str__(self) -> str:
        return "(" + str(self.x) + ", " + str(self.y) + ")"

a: Vector = Vector(3, 4)
b: Vector = Vector(1, 2)

c: Vector = a + b
print(c)

d: Vector = a - b
print(d)

e: Vector = a * b
print(e)
