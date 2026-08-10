"""Generators driven by loops, and the values they yield.

"yield a, b" parsed only the first expression instead of building a tuple.
And the optimized "for i in range(n)" loop kept its bounds in temporaries,
which a generator does not preserve across a yield, so the loop ran exactly
one iteration and then compared against a stale value.
"""


def squares(limit: int):
    for index in range(limit):
        yield index, index * index


def triangle(rows: int):
    for row in range(rows):
        for column in range(row + 1):
            yield row, column


def stepped(start: int, stop: int, step: int):
    for value in range(start, stop, step):
        yield value


def countdown(start: int):
    for value in range(start, 0, -1):
        yield value


def running_total(values):
    total = 0
    for value in values:
        total += value
        yield value, total


def chained(limit: int):
    yield from squares(limit)
    yield "end", 0


def early_exit(limit: int):
    for index in range(limit):
        if index > 2:
            return
        yield index


def with_else(limit: int):
    for index in range(limit):
        yield index
    else:
        yield -1


print(list(squares(4)))
print(list(squares(0)))
print(list(squares(1)))
print(list(triangle(3)))
print(list(stepped(1, 10, 3)))
print(list(countdown(4)))
print(list(running_total([5, 3, 2])))
print(list(chained(2)))
print(list(early_exit(6)))
print(list(with_else(2)))

print(tuple(squares(3)))
print(sum(value for value, _ in squares(4)))

pair_generator = squares(3)
print(next(pair_generator), next(pair_generator))

collected = []
for row, column in triangle(3):
    collected.append(str(row) + ":" + str(column))
print(collected)


class Series:
    def __init__(self, factor: int) -> None:
        self.factor = factor

    def scaled(self, count: int):
        for index in range(count):
            yield index, index * self.factor

    def nested(self, count: int):
        for index in range(count):
            yield from self.scaled(index)

    def bounded(self, count: int, limit: int):
        produced = 0
        for index in range(count):
            if produced >= limit:
                return
            produced += 1
            yield index


series = Series(10)
print(list(series.scaled(3)))
print(list(series.nested(3)))
print(list(series.bounded(10, 3)))


def accumulate(initial: int = 0):
    total = initial
    while True:
        received = yield total
        if received is None:
            break
        total += received


accumulator = accumulate(10)
print(next(accumulator))
print(accumulator.send(5))
print(accumulator.send(7))


def deep(levels: int):
    for outer in range(levels):
        for middle in range(outer + 1):
            for inner in range(middle + 1):
                yield outer, middle, inner


print(len(list(deep(3))))
print(list(deep(2)))
