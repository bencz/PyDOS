# Test enumerate builtin (Python-backed via stdlib PIR)
# Basic usage
for i, v in enumerate(["a", "b", "c"]):
    print(i)
    print(v)
# Empty iterable
for i, v in enumerate([]):
    print(i)
# Single element
for i, v in enumerate(["only"]):
    print(i)
    print(v)
# Convert to list
result: list = list(enumerate([10, 20, 30]))
print(len(result))
