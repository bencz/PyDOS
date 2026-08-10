# Container stdlib behavior implemented in Python PIR.

def opaque(value) -> object:
    return value

# list.sort: stable key ordering and Python 3 keyword shape.
words: list = ["bbb", "a", "cc", "dd"]
words.sort(key=lambda word: len(word))
print(words)
words.sort(key=lambda word: len(word), reverse=True)
print(words)

# list.index and tuple.index bounds, including negative bounds.
numbers: list = [10, 20, 10, 30, 10]
print(numbers.index(10, 1))
print(numbers.index(10, -2))
try:
    print(numbers.index(10, 1, 2))
except ValueError:
    print("list bounds")

values: tuple = (1, 2, 1, 3, 1)
print(values.index(1, 1, 4))
print(values.index(1, -1))

# dict.get and setdefault own their defaults in Python, not in a C shortcut.
mapping: dict = {"present": 7}
print(mapping.get("present", 99))
print(mapping.get("missing", 99))
print(mapping.get("missing"))
print(mapping.setdefault("empty"))
print("empty" in mapping)

# A real set() constructor replaces the old {0}; clear() workaround.
made: set = set([1, 2, 2, 3])
print(len(made))
print(2 in made)

left: set = {1, 2, 3, 4}
left.intersection_update({2, 4, 6})
print(len(left))
print(2 in left)
print(1 in left)

left = {1, 2, 3, 4}
left.difference_update({2, 4, 6})
print(len(left))
print(1 in left)
print(2 in left)

left = {1, 2, 3}
left.symmetric_difference_update({3, 4, 5})
print(len(left))
print(3 in left)
print(5 in left)

# Force runtime method lookup to exercise builtin vtable registration.
dynamic_set: object = opaque({1, 2})
dynamic_union: set = dynamic_set.union({2, 3})
print(len(dynamic_union))
print(3 in dynamic_union)

dynamic_frozen: object = opaque(frozenset([1, 2, 3]))
dynamic_difference: frozenset = dynamic_frozen.difference(frozenset([2]))
print(len(dynamic_difference))
print(2 in dynamic_difference)
