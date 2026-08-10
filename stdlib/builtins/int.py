# int.py - Python-visible behavior for the integer primitive
from _internal import internal_implementation

class int:
    @internal_implementation("pydos_int_bit_length_")
    def bit_length(self) -> int:
        pass

    @internal_implementation("pydos_int_bit_count_")
    def bit_count(self) -> int:
        pass

    def as_integer_ratio(self) -> tuple:
        return (self, 1)

    def is_integer(self) -> bool:
        return True

    def conjugate(self) -> object:
        return self
