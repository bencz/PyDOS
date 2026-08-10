"""Nested yield from chains."""


def a() -> object:
    yield 1
    yield 2


def b() -> object:
    yield from a()
    yield 3


def c() -> object:
    yield from b()
    yield 4


def deep1() -> object:
    yield "x"


def deep2() -> object:
    yield from deep1()
    yield "y"


def deep3() -> object:
    yield from deep2()
    yield "z"


def deep4() -> object:
    yield from deep3()
    yield "w"


def main() -> None:
    x: object

    # Test 1: Three-level nesting
    for x in c():
        print(x)

    # Test 2: Four-level nesting
    for x in deep4():
        print(x)


main()
