"""Test star unpacking in assignment targets (PEP 3132)."""

def test_middle() -> None:
    a: int
    c: int
    b: list
    a, *b, c = [1, 2, 3, 4, 5]
    print(a)
    print(b)
    print(c)

def test_rest() -> None:
    first: int
    rest: list
    first, *rest = [10, 20, 30]
    print(first)
    print(rest)

def test_init() -> None:
    init: list
    last: int
    *init, last = [10, 20, 30]
    print(init)
    print(last)

def test_empty_star() -> None:
    a: int
    b: list
    c: int
    a, *b, c = [1, 2]
    print(a)
    print(b)
    print(c)

def test_tuple_src() -> None:
    x: int
    y: int
    rest: list
    x, y, *rest = (100, 200, 300, 400)
    print(x)
    print(y)
    print(rest)

def test_single_star() -> None:
    star: list
    *star, = [7, 8, 9]
    print(star)

def main() -> None:
    test_middle()
    test_rest()
    test_init()
    test_empty_star()
    test_tuple_src()
    test_single_star()

main()
