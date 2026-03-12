# dn_init.py - Dunder init test
# Tests: __init__ with various field setup

class Point:
    def __init__(self, x: int, y: int, z: int) -> None:
        self.x = x
        self.y = y
        self.z = z

    def __str__(self) -> str:
        return str(self.x) + "," + str(self.y) + "," + str(self.z)

p: Point = Point(1, 2, 3)
print(p)

q: Point = Point(10, 20, 30)
print(q)
