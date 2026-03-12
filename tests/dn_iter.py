# dn_iter.py - Dunder iterator tests
# Tests: __iter__, __next__

class Counter:
    def __init__(self, start: int, stop: int) -> None:
        self.current = start
        self.stop = stop

    def __iter__(self) -> Counter:
        return self

    def __next__(self) -> int:
        if self.current >= self.stop:
            raise StopIteration()
        val: int = self.current
        self.current = self.current + 1
        return val

c: Counter = Counter(1, 5)
for x in c:
    print(x)
