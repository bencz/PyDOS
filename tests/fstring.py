# Test: F-string formatting
# Tests f-string with expressions, variables, and literals

def test_fstrings() -> None:
    name: str = "World"
    print(f"Hello, {name}!")

    x: int = 42
    print(f"The answer is {x}")

    a: int = 3
    b: int = 4
    print(f"{a} + {b} = {a + b}")

    greeting: str = f"Hi {name}"
    print(greeting)

    # Nested expression
    print(f"double: {x * 2}")

    # Empty f-string parts
    print(f"start{name}end")

test_fstrings()
