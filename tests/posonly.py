"""Test positional-only parameters (PEP 570, / separator)."""

def add(a: int, b: int, /) -> int:
    return a + b

def mixed(a: int, b: int, /, c: int, d: int) -> int:
    return a + b + c + d

def with_default(a: int, b: int = 10, /) -> int:
    return a + b

def posonly_and_kwonly(a: int, /, b: int, *, c: int) -> int:
    return a + b + c

def main() -> None:
    print(add(1, 2))
    print(mixed(1, 2, 3, 4))
    print(mixed(1, 2, c=3, d=4))
    print(with_default(5))
    print(with_default(5, 20))
    print(posonly_and_kwonly(1, 2, c=3))
    print(posonly_and_kwonly(1, b=2, c=3))

main()
