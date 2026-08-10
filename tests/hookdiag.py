from copy import copy, deepcopy


class RichValue:
    def __init__(self, values):
        self.values = list(values)

    def __eq__(self, other):
        if not isinstance(other, RichValue):
            return NotImplemented
        return self.values == other.values

    def __reversed__(self):
        return iter(self.values[::-1])

    def __format__(self, format_spec):
        separator = format_spec or ","
        return separator.join(str(value) for value in self.values)

    def __bytes__(self):
        return bytes(self.values)

    def __copy__(self):
        return type(self)(self.values)

    def __deepcopy__(self, memo):
        duplicate = type(self)(deepcopy(self.values, memo))
        memo[id(self)] = duplicate
        return duplicate


value = RichValue([1, 2, 3])
print(tuple(reversed(value)))
print(f"{value:|}")
print(bytes(value))
shallow = copy(value)
print(shallow == value, shallow is not value)
deep = deepcopy(value)
print(deep == value, deep is not value, deep.values is not value.values)
