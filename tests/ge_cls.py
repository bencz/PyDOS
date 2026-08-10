"""Generator close() test."""


def gen_simple() -> object:
    yield 1
    yield 2
    yield 3


def main() -> None:
    # Test 1: Close active generator
    g: object = gen_simple()
    print(next(g))
    g.close()
    print("after close")

    # Test 2: Close exhausted generator — no-op
    g2: object = gen_simple()
    print(next(g2))
    print(next(g2))
    print(next(g2))
    g2.close()
    print("close exhausted ok")

    # Test 3: Close unstarted generator
    g3: object = gen_simple()
    g3.close()
    print("close unstarted ok")


main()
