# list.py - Python list class for PyDOS

from _internal import internal_implementation

class list:
    @internal_implementation("pydos_list_get_op_")
    def __getitem__(self, index: int):
        pass

    @internal_implementation("pydos_list_set_op_")
    def __setitem__(self, index: int, value) -> None:
        pass

    @internal_implementation("pydos_list_slice_op_")
    def __getslice__(self, start: int, stop: int, step: int):
        pass

    @internal_implementation("pydos_list_append_")
    def append(self, item) -> None:
        pass

    @internal_implementation("pydos_list_insert_m_")
    def insert(self, index: int, item) -> None:
        pass

    def extend(self, iterable) -> None:
        for item in iterable:
            self.append(item)

    def remove(self, item) -> None:
        i: int = 0
        for value in self:
            if value == item:
                self.pop(i)
                return
            i = i + 1
        raise ValueError("list.remove(x): x not in list")

    @internal_implementation("pydos_list_clear_m_")
    def clear(self) -> None:
        pass

    @internal_implementation("pydos_list_reverse_m_")
    def reverse(self) -> None:
        pass

    def sort(self, key=None, reverse: bool = False) -> None:
        if reverse is None:
            reverse = False

        # Stable insertion sort.  C retains indexed storage access while the
        # ordering policy (key/reverse) lives in the Python standard library.
        i: int = 1
        while i < len(self):
            current = self[i]
            if key is None:
                current_key = current
            else:
                current_key = key(current)

            j: int = i - 1
            while j >= 0:
                other = self[j]
                if key is None:
                    other_key = other
                else:
                    other_key = key(other)

                move: bool = other_key > current_key
                if reverse:
                    move = other_key < current_key
                if not move:
                    break
                self[j + 1] = other
                j = j - 1

            self[j + 1] = current
            i = i + 1

    @internal_implementation("pydos_list_pop_m_")
    def pop(self, index: int = -1):
        pass

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
        raise ValueError("list.index(x): x not in list")

    def count(self, item) -> int:
        count: int = 0
        for value in self:
            if value == item:
                count = count + 1
        return count

    def copy(self) -> list:
        result: list = []
        for value in self:
            result.append(value)
        return result
