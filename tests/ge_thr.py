"""Generator throw() — exceptions propagate to caller."""


def gen_simple() -> object:
    yield 1
    yield 2


def main() -> None:
    # Test 1: Throw into active generator — propagates
    g: object = gen_simple()
    print(next(g))
    try:
        g.throw(ValueError("oops"))
    except ValueError:
        print("ValueError propagated")

    # Test 2: Throw into exhausted generator — propagates
    g2: object = gen_simple()
    print(next(g2))
    print(next(g2))
    try:
        g2.throw(ValueError("late"))
    except ValueError:
        print("ValueError propagated again")


main()
