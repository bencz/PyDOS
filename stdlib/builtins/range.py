# range.py - Python policy for the primitive range type.

from _internal import internal_implementation

class range:
    @internal_implementation("pydos_range_getitem_")
    def __getitem__(self, index: int):
        pass

    @internal_implementation("pydos_range_slice_op_")
    def __getslice__(self, start: int, stop: int, step: int):
        pass

    @internal_implementation("pydos_range_len_")
    def __len__(self) -> int:
        pass

    @internal_implementation("pydos_obj_contains_")
    def __contains__(self, value) -> bool:
        pass

    def count(self, value) -> int:
        count: int = 0
        for item in self:
            if item == value:
                count = count + 1
        return count

    def index(self, value) -> int:
        index: int = 0
        for item in self:
            if item == value:
                return index
            index = index + 1
        raise ValueError("range.index(x): x not in range")
