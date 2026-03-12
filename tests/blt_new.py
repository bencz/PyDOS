# Test new builtin functions
items: list = [1, 2, 3, 4, 5]

# sum
print(sum(items))

# any / all
print(any([0, 0, 1]))
print(any([0, 0, 0]))
print(all([1, 2, 3]))
print(all([1, 0, 3]))

# reversed (generator - collect via for loop)
rev: list = []
for x in reversed(items):
    rev.append(x)
print(rev[0])
print(rev[4])

# enumerate (generator - collect via for loop)
pairs: list = []
for p in enumerate(["a", "b", "c"]):
    pairs.append(p)
t0: tuple = pairs[0]
print(t0[0])
print(t0[1])

# zip (generator - collect via for loop)
z: list = []
for p in zip([1, 2, 3], ["x", "y", "z"]):
    z.append(p)
zt: tuple = z[0]
print(zt[0])
print(zt[1])

# repr / hash
print(repr(42))
h: int = hash("hello")
print(h > 0)

# id
x: int = 42
i: int = id(x)
print(i > 0)
