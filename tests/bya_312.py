# Python-backed bytearray behavior and primitive mutation bridges.

def opaque(value) -> object:
    return value

data: bytearray = bytearray([1, 2, 2, 3])
print(data.__len__())
data.extend([4, 5])
print(len(data))
print(data.count(2))
print(data.index(2))
print(data.index(2, 2))

data.insert(0, 9)
print(data[0])
print(len(data))
data.insert(-1, 8)
print(data[-2])

print(data.pop(1))
print(len(data))
data.remove(2)
print(data.count(2))

copied: bytearray = data.copy()
copied[0] = 7
print(data[0])
print(copied[0])

data.reverse()
print(data[0])
print(data[-1])

raw: bytearray = bytearray([0, 15, 16, 255])
print(raw.hex())
print(raw.hex(":"))
print(bytearray([72, 195, 136]).decode("utf-8") == ("H" + chr(200)))

sliced: bytearray = bytearray([10, 20, 30, 40])[1:3]
print(len(sliced))
print(sliced[0])
print(sliced[1])
joined: bytearray = bytearray([1, 2]) + bytearray([3])
print(len(joined))
print(joined[2])
repeated: bytearray = bytearray([7, 8]) * 3
print(len(repeated))
print(repeated[4])

dynamic: object = opaque(bytearray([1, 2, 2, 3]))
print(dynamic.count(2))
dynamic.reverse()
print(dynamic[0])

try:
    bytearray([-1])
except ValueError:
    print("range-error")

try:
    bytearray(["x"])
except TypeError:
    print("type-error")

try:
    data.append(256)
except ValueError:
    print("append-error")

try:
    bytearray().pop()
except IndexError:
    print("pop-error")

try:
    data.index(99)
except ValueError:
    print("index-error")

try:
    print(dynamic[99])
except IndexError:
    print("subscript-error")

try:
    dynamic[99] = 1
except IndexError:
    print("assignment-error")

try:
    dynamic[0] = 256
except ValueError:
    print("assignment-range-error")
