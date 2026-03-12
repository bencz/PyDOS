def greet(name: str, greeting: str = "Hello") -> None:
    print(greeting + " " + name)

def add(a: int, b: int = 10) -> int:
    return a + b

def make_list(x: int, y: int = 0, z: int = 0) -> None:
    print(x)
    print(y)
    print(z)

def main() -> None:
    # Call with all args
    greet("World", "Hi")

    # Call with default
    greet("DOS")

    # Arithmetic with default
    result: int = add(5)
    print(result)

    result = add(5, 20)
    print(result)

    # Multiple defaults
    make_list(1)
    make_list(1, 2)
    make_list(1, 2, 3)

main()
