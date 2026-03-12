class Point:
    def __init__(self: object, x: int, y: int) -> None:
        self.x = x
        self.y = y

    def __eq__(self: object, other: object) -> bool:
        return self.x == other.x and self.y == other.y

    def __lt__(self: object, other: object) -> bool:
        return self.x < other.x

    def __str__(self: object) -> str:
        return "Point(" + str(self.x) + ", " + str(self.y) + ")"

    def __len__(self: object) -> int:
        return self.x + self.y

def main() -> None:
    p1: object = Point(1, 2)
    p2: object = Point(1, 2)
    p3: object = Point(3, 4)

    # Test __str__
    print(p1)
    print(p3)

    # Test __eq__
    if p1 == p2:
        print("p1 == p2")
    if p1 == p3:
        print("p1 == p3")
    else:
        print("p1 != p3")

    # Test __lt__ via comparison
    if p1 < p3:
        print("p1 < p3")

    # Test __len__
    n: int = len(p3)
    print(n)

main()
