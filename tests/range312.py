# Python 3.12 range behavior and shared primitive slicing rules.

values: range = range(2, 12, 3)
print(len(values))
print(values.start)
print(values.stop)
print(values.step)
print(values[0])
print(values[-1])
print(5 in values)
print(6 in values)
print(5.0 in values)
print(values.count(5))
print(values.count(6))
print(values.index(8))

middle: range = values[1:3]
print(middle)
print(list(middle))
print(list(range(5)[::-1]))
print(range(0) == range(2, 1, 3))
print(range(0, 6, 2) == range(0, 5, 2))

print("abcd"[::-1])
print([1, 2, 3][::-1])
print(bytes([1, 2, 3])[::-1])
print(bytearray([1, 2, 3])[::-1])

try:
    range(1, 5, 0)
except ValueError:
    print("zero step")

try:
    print(values[99])
except IndexError:
    print("bad index")

try:
    print(values.index(99))
except ValueError:
    print("missing value")
