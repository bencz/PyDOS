# tp_fzs.py - frozenset type integration test

# Basic frozenset from list
fs = frozenset([3, 1, 2, 1])
print(len(fs))

# Contains
print(1 in fs)
print(99 in fs)

# Iteration
result = []
for x in fs:
    result.append(x)
print(len(result))

# Empty frozenset
empty = frozenset()
print(len(empty))

# frozenset from set
s = {10, 20, 30}
fs2 = frozenset(s)
print(len(fs2))
print(10 in fs2)

# Equality
a = frozenset([1, 2, 3])
b = frozenset([3, 2, 1])
print(a == b)

print("done")
