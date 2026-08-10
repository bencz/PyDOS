# tuple.py - Python tuple class for PyDOS

class tuple:
    def count(self, item) -> int:
        count: int = 0
        for value in self:
            if value == item:
                count = count + 1
        return count

    def index(self, item, start: int = 0, stop=None) -> int:
        n: int = len(self)
        if start is None:
            start = 0
        if stop is None:
            stop = n
        if start < 0:
            start = start + n
            if start < 0:
                start = 0
        if stop < 0:
            stop = stop + n
        if stop > n:
            stop = n

        i: int = start
        while i < stop:
            if self[i] == item:
                return i
            i = i + 1
        raise ValueError("tuple.index(x): x not in tuple")
