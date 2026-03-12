# Test: Float arithmetic operations
# Tests float literals, arithmetic, comparison, and mixed int/float

def test_floats() -> None:
    # Basic float
    x: float = 3.14
    print(x)

    # Float arithmetic
    a: float = 2.5
    b: float = 1.5
    print(a + b)
    print(a - b)
    print(a * b)

    # Float negation
    c: float = -2.5
    print(c)

    # Int + float promotion
    n: int = 10
    f: float = 0.5
    result: float = n + f
    print(result)

    # Float comparison
    if a > b:
        print("2.5 > 1.5")
    if b < a:
        print("1.5 < 2.5")
    if a == 2.5:
        print("a == 2.5")

    # Float to string conversion
    print(str(3.14))

test_floats()
