"""complex type stub for PyDOS stdlib."""

from _internal import internal_implementation

class complex:
    """Complex number type with real and imaginary parts."""

    @internal_implementation("pydos_complex_conjugate_")
    def conjugate(self) -> complex:
        """Return the complex conjugate."""
        pass
