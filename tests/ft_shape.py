# ft_shape.py - Shape hierarchy with polymorphism

class Shape:
    def __init__(self, name: str) -> None:
        self.name = name

    def area(self) -> int:
        return 0

    def perimeter(self) -> int:
        return 0

    def describe(self) -> str:
        return self.name + ": area=" + str(self.area()) + " perim=" + str(self.perimeter())

class Rectangle(Shape):
    def __init__(self, w: int, h: int) -> None:
        super().__init__("Rect")
        self.w = w
        self.h = h

    def area(self) -> int:
        return self.w * self.h

    def perimeter(self) -> int:
        return 2 * (self.w + self.h)

class Square(Rectangle):
    def __init__(self, side: int) -> None:
        super().__init__(side, side)
        self.name = "Square"

class Triangle(Shape):
    def __init__(self, base: int, height: int, s1: int, s2: int, s3: int) -> None:
        super().__init__("Tri")
        self.base = base
        self.height = height
        self.s1 = s1
        self.s2 = s2
        self.s3 = s3

    def area(self) -> int:
        return self.base * self.height // 2

    def perimeter(self) -> int:
        return self.s1 + self.s2 + self.s3

r: Rectangle = Rectangle(6, 4)
sq: Square = Square(5)
tri: Triangle = Triangle(6, 4, 5, 5, 6)
print(r.describe())
print(sq.describe())
print(tri.describe())
print(r.area())
print(sq.area())
print(tri.area())
