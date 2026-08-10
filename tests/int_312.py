# Python 3.12 integer methods split between the numeric runtime and stdlib.

def opaque(value) -> object:
    return value

print((0).bit_length())
print((255).bit_length())
print((-255).bit_length())
print((0).bit_count())
print((181).bit_count())
print((-181).bit_count())

ratio: tuple = (-37).as_integer_ratio()
print(ratio[0])
print(ratio[1])
print((42).is_integer())
print((-9).conjugate())

dynamic: object = opaque(13)
print(dynamic.bit_count())
dynamic_ratio: tuple = dynamic.as_integer_ratio()
print(dynamic_ratio[0])
print(dynamic_ratio[1])
print(dynamic.is_integer())
print(dynamic.conjugate())
