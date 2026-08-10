"""yield from — basic delegation test."""


def inner() -> object:
    yield 1
    yield 2
    yield 3


def outer() -> object:
    yield from inner()
    yield 4


def letters() -> object:
    yield "a"
    yield "b"


def numbers() -> object:
    yield 10
    yield 20


def combined() -> object:
    yield from letters()
    yield from numbers()
    yield "end"


def main() -> None:
    x: object

    # Test 1: Basic yield from
    for x in outer():
        print(x)

    # Test 2: Multiple yield from in sequence
    for x in combined():
        print(x)

    # Test 3: yield from a list
    def from_list() -> object:
        yield from [100, 200, 300]

    for x in from_list():
        print(x)


main()
