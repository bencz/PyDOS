# Test reversed builtin (Python-backed via stdlib PIR)
# Basic usage
for v in reversed([1, 2, 3]):
    print(v)
# String list
for v in reversed(["a", "b", "c"]):
    print(v)
# Single element
for v in reversed([42]):
    print(v)
# Empty
for v in reversed([]):
    print(v)
# Convert to list
result: list = list(reversed([10, 20, 30]))
print(len(result))
