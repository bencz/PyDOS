# Python 3.12 float methods and numeric primitive properties.

def opaque(value) -> object:
    return value

print((4.0).is_integer())
print((4.25).is_integer())
print((-7.0).conjugate())
print((5).real)
print((5).imag)
print((5).numerator)
print((5).denominator)
print((2.5).real)
print((2.5).imag)
print(7.5 // 2.0)
print(-7.5 // 2.0)
print(7.5 % 2.0)
print(-7.5 % 2.0)
print(2 ** -2)

dynamic: object = opaque(9.0)
print(dynamic.is_integer())
print(dynamic.conjugate())
print(dynamic.real)
print(dynamic.imag)

try:
    value = (1+2j) / (0+0j)
    print(value)
except ZeroDivisionError:
    print("complex-zero")
