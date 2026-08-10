# Test: Nested function definitions used as local functions
# Tests functions defined inside other functions, callable locally

def test_nested() -> None:
    def double(x: int) -> int:
        return x * 2

    print(double(5))
    print(double(21))

    def add(a: int, b: int) -> int:
        return a + b

    print(add(3, 4))
    print(add(10, 20))

    # Nested function with string
    def greet(name: str) -> str:
        return "Hi, " + name

    print(greet("Alice"))
    print(greet("Bob"))

test_nested()
