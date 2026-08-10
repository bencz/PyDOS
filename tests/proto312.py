print(round(2.5))
print(round(3.5))
print(round(-2.5))
print(round(1.25, 1))
print(NotImplemented)
print(f"{[1, 2]!r}")
print(isinstance(1, (str, int)))
print(issubclass(bool, (str, int)))


class LeftOperand:
    def __mul__(self, other):
        return NotImplemented


class RightOperand:
    def __rmul__(self, other):
        return "reflected"


print(LeftOperand() * RightOperand())
