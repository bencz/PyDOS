class Sample:
    def __init__(self, value):
        self._value = value

    @staticmethod
    def add(left, right):
        return left + right

    @classmethod
    def identify(cls, suffix):
        return cls.__name__ + suffix

    @property
    def value(self):
        return self._value

    @value.setter
    def value(self, new_value):
        self._value = new_value * 2

    @value.deleter
    def value(self):
        self._value = -1


item = Sample(21)
print(Sample.add(2, 3))
print(item.add(4, 5))
print(Sample.identify("!"))
print(item.identify("?"))
print(item.value)
item.value = 8
print(item.value)
del item.value
print(item.value)
print(Sample.value is Sample.value)
