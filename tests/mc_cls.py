# Test class patterns in match/case
class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

class Circle:
    def __init__(self, r: int) -> None:
        self.r = r

def check_shape(s) -> None:
    match s:
        case Point(x=a, y=b):
            print(a)
            print(b)
        case Circle(r=radius):
            print(radius)
        case _:
            print("unknown")

# Point match
check_shape(Point(3, 4))

# Circle match
check_shape(Circle(10))

# No match
check_shape(42)

# Nested class pattern with literal sub-pattern
def check_origin(p) -> None:
    match p:
        case Point(x=0, y=0):
            print("origin")
        case Point(x=x, y=0):
            print("on x-axis")
            print(x)
        case Point(x=a, y=b):
            print("other")

check_origin(Point(0, 0))
check_origin(Point(5, 0))
check_origin(Point(1, 2))
