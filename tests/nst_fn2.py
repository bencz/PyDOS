# Test: Nested functions (no closures)
# Tests functions defined inside other functions

def outer() -> None:
    def inner(x: int) -> int:
        return x * 2

    result: int = inner(5)
    print(result)

    def add(a: int, b: int) -> int:
        return a + b

    print(add(3, 4))

def make_greeting(name: str) -> str:
    def prefix() -> str:
        return "Hello"

    return prefix() + ", " + name + "!"

print(make_greeting("World"))

outer()
