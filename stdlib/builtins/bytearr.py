# bytearr.py - Python bytearray class for PyDOS (8.3 filename)

from _internal import internal_implementation, type_operators

@type_operators(getitem="pydos_bytearray_getitem_", setitem="pydos_bytearray_setitem_", contains="pydos_obj_contains_")
class bytearray:
    @internal_implementation("pydos_bytearray_append_m_")
    def append(self, byte: int) -> None:
        pass

    def extend(self, data) -> None:
        for byte in data:
            self.append(byte)

    @internal_implementation("pydos_bytearray_insert_m_")
    def insert(self, index: int, byte: int) -> None:
        pass

    @internal_implementation("pydos_bytearray_pop_m_")
    def pop(self, index: int = -1) -> int:
        pass

    @internal_implementation("pydos_bytearray_clear_m_")
    def clear(self) -> None:
        pass

    @internal_implementation("pydos_bytearray_len_m_")
    def __len__(self) -> int:
        pass

    def copy(self) -> bytearray:
        values: list = []
        for byte in self:
            values.append(byte)
        return _pydos_bytearray_from_list(values)

    def count(self, value: int) -> int:
        result: int = 0
        for byte in self:
            if byte == value:
                result = result + 1
        return result

    def index(self, value: int, start: int = 0, stop=None) -> int:
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
            if self[i] == value:
                return i
            i = i + 1
        raise ValueError("bytearray.index(x): x not in bytearray")

    def remove(self, value: int) -> None:
        index: int = self.index(value)
        self.pop(index)

    def reverse(self) -> None:
        left: int = 0
        right: int = len(self) - 1
        while left < right:
            value: int = self[left]
            self[left] = self[right]
            self[right] = value
            left = left + 1
            right = right - 1

    def hex(self, sep=None, bytes_per_sep: int = 1) -> str:
        return bytes(self).hex(sep, bytes_per_sep)

    def decode(self, encoding: str = "utf-8", errors: str = "strict") -> str:
        return bytes(self).decode(encoding, errors)
