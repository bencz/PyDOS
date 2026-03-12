class Pair[T]:
    first: T
    second: T

    def __init__(self, first: T, second: T) -> None:
        self.first = first
        self.second = second

    def __add__(self, other: Pair[T]) -> Pair[T]:
        return Pair(self.first + other.first, self.second + other.second)

    def __str__(self) -> str:
        return str(self.first) + "," + str(self.second)

a: Pair[int] = Pair(3, 4)
b: Pair[int] = Pair(1, 2)
c: Pair[int] = a + b
print(c)
print(str(a) + " + " + str(b) + " = " + str(c))
