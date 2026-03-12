# Test: Two-type-parameter generics
# Tests generic classes with two type parameters

class Container[T]:
    def __init__(self, value: T) -> None:
        self.value: T = value

    def get(self) -> T:
        return self.value

def test_generics() -> None:
    # Instantiate with int
    ci: Container[int] = Container[int](42)
    print(ci.get())

    # Instantiate with str
    cs: Container[str] = Container[str]("hello")
    print(cs.get())

    # Use dict with two type params (str keys, int values)
    d: dict = {"x": 10, "y": 20}
    print(d["x"])
    print(d["y"])

    # Multiple containers
    c1: Container[int] = Container[int](100)
    c2: Container[str] = Container[str]("world")
    print(c1.get())
    print(c2.get())

test_generics()
