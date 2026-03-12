# del statement: attribute deletion

class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

    def __str__(self) -> str:
        return "Point"

p: Point = Point(10, 20)
print(p.x)
print(p.y)

# Delete attribute x
del p.x

# Set it again with new value
p.x = 99
print(p.x)
print(p.y)
