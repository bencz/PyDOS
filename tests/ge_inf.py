"""Infinite generators with send."""


def accumulator() -> object:
    total: int = 0
    while True:
        val: object = yield total
        if val is None:
            break
        total = total + val


def counter(start: int) -> object:
    n: int = start
    while True:
        step: object = yield n
        if step is None:
            n = n + 1
        else:
            n = n + step


def main() -> None:
    # Test 1: Accumulator
    g: object = accumulator()
    next(g)
    print(g.send(10))
    print(g.send(20))
    print(g.send(5))

    # Test 2: Counter with variable step
    c: object = counter(0)
    print(next(c))
    print(next(c))
    print(c.send(10))
    print(next(c))
    print(c.send(100))


main()
