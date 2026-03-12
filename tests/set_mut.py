# Test set mutation methods: add, remove, discard, clear, pop
s = {1, 2, 3}
s.add(4)
print(len(s))
print(4 in s)

s.remove(2)
print(len(s))
print(2 in s)

s.discard(3)
print(len(s))

s.discard(99)
print(len(s))

# pop from single-element set (deterministic)
t = {10}
item = t.pop()
print(item)
print(len(t))

# clear
u = {1, 2, 3}
u.clear()
print(len(u))

# remove raises KeyError
try:
    v = {1, 2}
    v.remove(99)
except KeyError:
    print("caught KeyError")
