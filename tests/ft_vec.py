# ft_vec.py - Vec3 operator overloading

class Vec3:
    def __init__(self, x: int, y: int, z: int) -> None:
        self.x = x
        self.y = y
        self.z = z

    def __add__(self, o: Vec3) -> Vec3:
        return Vec3(self.x + o.x, self.y + o.y, self.z + o.z)

    def __sub__(self, o: Vec3) -> Vec3:
        return Vec3(self.x - o.x, self.y - o.y, self.z - o.z)

    def __str__(self) -> str:
        return "<" + str(self.x) + "," + str(self.y) + "," + str(self.z) + ">"

    def dot(self, o: Vec3) -> int:
        return self.x * o.x + self.y * o.y + self.z * o.z

    def length_sq(self) -> int:
        return self.dot(self)

a: Vec3 = Vec3(1, 2, 3)
b: Vec3 = Vec3(4, 5, 6)
c: Vec3 = a + b
d: Vec3 = b - a
print(a)
print(b)
print(c)
print(d)
print(a.dot(b))
print(c.length_sq())
