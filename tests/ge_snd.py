"""Generator send() test — echo generator and accumulator."""


def echo() -> object:
    val: object = yield "ready"
    while val is not None:
        val = yield val


def accumulator() -> object:
    total: int = 0
    while True:
        val: object = yield total
        if val is None:
            break
        total = total + val


def main() -> None:
    # Test 1: Echo generator — send values back
    g: object = echo()
    print(next(g))
    print(g.send(1))
    print(g.send(2))
    print(g.send("hello"))

    # Test 2: Accumulator — accumulate sent values
    a: object = accumulator()
    next(a)
    print(a.send(10))
    print(a.send(20))
    print(a.send(5))

    # Test 3: send(None) is equivalent to next()
    g2: object = echo()
    print(g2.send(None))


main()
