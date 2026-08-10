class SequenceView:
    def __init__(self, values):
        self.values = values

    def __getitem__(self, index):
        return self.values[index]


values = SequenceView([0, 1, 2, 3, 4])

print(values[1:])
print(values[:3])
print(values[::2])
print(values[::-1])
print(values[-4:-1])
print((0, 1, 2, 3)[1:3])
print("python"[1:5:2])


class GenericSequence[T]:
    def __init__(self, values):
        self.values = values

    def __getitem__(self, index: int | slice):
        return self.values[index]


generic = GenericSequence([1, 2, 3])
print(generic[0])
print(generic[1:])
