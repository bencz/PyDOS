type Pair[T] = tuple[T, T]
type Packed[*Ts] = tuple[str, *Ts]
type Callback[**P] = tuple[P, int]


class Box[T]:
    pass


class TextBox[T: str](Box[T]):
    pass


class NumberBox[T: (int, float)](Box[T]):
    pass


class Repository[T: Entity]:
    pass


class Entity:
    pass


def identity[T](value):
    return value


print(Box.__type_params__[0].__name__)
print(identity.__type_params__[0].__name__)
print(Pair.__name__)
print(Pair.__type_params__[0].__name__)
print(Pair.__value__ == tuple[Pair.__type_params__[0], Pair.__type_params__[0]])
print(len(Packed.__type_params__))
print(Callback.__type_params__[0].__name__)
print(TextBox.__type_params__[0].__bound__ is str)
print(NumberBox.__type_params__[0].__constraints__ == (int, float))
print(Repository.__type_params__[0].__bound__ is Entity)
