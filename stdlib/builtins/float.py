# float.py - Python-visible behavior for the floating-point primitive

from _internal import internal_implementation

class float:
    @internal_implementation("pydos_float_is_integer_")
    def is_integer(self) -> bool:
        pass

    def conjugate(self) -> object:
        return self
