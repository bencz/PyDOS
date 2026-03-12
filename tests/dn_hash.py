# dn_hash.py - Custom hash dunder
class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y
    def __hash__(self) -> int:
        return self.x * 1000 + self.y
    def __eq__(self, other: Point) -> bool:
        return self.x == other.x and self.y == other.y
    def __str__(self) -> str:
        return "(" + str(self.x) + "," + str(self.y) + ")"

p1: Point = Point(1, 2)
p2: Point = Point(3, 4)
p3: Point = Point(1, 2)

print(hash(p1) == hash(p3))
print(hash(p1) == hash(p2))
print(p1 == p3)
print(p1 == p2)

# Test: class without __hash__ — address-based fallback
class Simple:
    def __init__(self, v: int) -> None:
        self.v = v

a: Simple = Simple(1)
# Same object hashes to same value (consistent)
print(hash(a) == hash(a))

# Test: inherited __hash__ from parent
class Point3D(Point):
    def __init__(self, x: int, y: int, z: int) -> None:
        self.x = x
        self.y = y
        self.z = z

q1: Point3D = Point3D(1, 2, 9)
q2: Point3D = Point3D(1, 2, 7)
# Inherited hash uses x*1000+y, ignores z
print(hash(q1) == hash(q2))
