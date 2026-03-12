# Test zip builtin (Python-backed via stdlib PIR)
# Basic usage
for a, b in zip([1, 2, 3], ["x", "y", "z"]):
    print(a)
    print(b)
# Different lengths (shorter wins)
for a, b in zip([1, 2], [10, 20, 30]):
    print(a)
    print(b)
# Empty
for a, b in zip([], [1, 2]):
    print(a)
# Single element
for a, b in zip([1], [2]):
    print(a)
    print(b)
