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

# High-level operations are Python-backed; C only owns frozenset storage,
# hashing, membership, iteration and construction.
u = a.union(frozenset([3, 4]))
print(len(u))
print(4 in u)

i = a.intersection(frozenset([2, 3, 4]))
print(len(i))

d = a.difference(frozenset([2, 9]))
print(len(d))
print(2 in d)

sd = a.symmetric_difference(frozenset([3, 4]))
print(len(sd))
print(4 in sd)

print(frozenset([1, 2]).issubset(a))
print(a.issuperset(frozenset([2])))
print(a.isdisjoint(frozenset([8, 9])))
print(a.copy() is a)

print("done")
