class Transform[T]:
    def __init__(self, value):
        self.__value = value

    def values(self, *functions):
        yield self.__value
        computed = tuple(function(self.__value) for function in functions)
        yield from computed


item = Transform((2, 3, 5, 7))
print(tuple(item.values(len, sum, lambda value: value[::-1])))
