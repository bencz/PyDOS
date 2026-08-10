from dataclasses import (
    MISSING, asdict, astuple, dataclass, field, fields, is_dataclass, replace
)


@dataclass
class Point:
    x: int
    y: int = 2


point = Point(1)
print(point)
print(point == Point(1, 2))
print(point != Point(2, 2))
print(Point.__annotations__["x"] is int)
print(Point.y)
print(is_dataclass(Point))
print(is_dataclass(point))
print(fields(Point)[0].name)
print(fields(point)[1].default)
print(fields(point)[0].default is MISSING)
print(asdict(point))
print(astuple(point))
print(replace(point, y=9))


@dataclass()
class Bucket:
    value: int
    items: list = field(default_factory=list, repr=False)


first = Bucket(3)
second = Bucket(3)
first.items.append(7)
print(first)
print(first.items)
print(second.items)
print(first == second)


@dataclass
class Child(Point):
    z: int = 4


child = Child(5)
print(child)
print(astuple(child))


@dataclass(order=True)
class Ordered:
    priority: int
    payload: int = field(compare=False)


print(Ordered(1, 99) < Ordered(2, 0))
print(Ordered(1, 99) == Ordered(1, 0))


@dataclass
class PostInit:
    value: int

    def __post_init__(self):
        self.value = self.value * 2


print(PostInit(6))

try:
    Point()
    print("missing not rejected")
except TypeError:
    print("missing rejected")
