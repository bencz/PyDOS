# bytearr.py - bytearray type stub for PyDOS (8.3 filename)
#
# Parsed by stdgen.cpp to generate stdlib.idx entries.

from _internal import internal_implementation, type_operators

@type_operators(getitem="pydos_bytearray_getitem_", setitem="pydos_bytearray_setitem_", contains="pydos_obj_contains_")
class bytearray:
    @internal_implementation("pydos_bytearray_append_")
    def append(self, byte: int) -> None: ...

    @internal_implementation("pydos_bytearray_extend_")
    def extend(self, data) -> None: ...

    @internal_implementation("pydos_bytearray_pop_")
    def pop(self) -> int: ...

    @internal_implementation("pydos_bytearray_clear_")
    def clear(self) -> None: ...

    @internal_implementation("pydos_bytearray_len_")
    def __len__(self) -> int: ...
