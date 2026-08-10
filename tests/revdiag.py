class Reversible:
    def __init__(self, values):
        self.values = values

    def __reversed__(self):
        return iter(self.values[::-1])


value = Reversible([1, 2, 3])
print(value.values[::-1])


def make_iterator():
    return iter([3, 2, 1])


print(tuple(make_iterator()))
print(tuple(value.__reversed__()))
print(tuple(reversed(value)))
