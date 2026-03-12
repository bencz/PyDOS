# del statement: local and global variable deletion

# del local variable
x: int = 10
print(x)
del x
# x is now deleted — we can reassign
x = 20
print(x)

# del global variable at module level
g: int = 100
print(g)
del g
# g is now deleted — reassign
g = 200
print(g)

# del inside a function (local scope)
def test_del_local() -> None:
    a: int = 42
    print(a)
    del a
    a = 99
    print(a)

test_del_local()

# del multiple targets
p: int = 1
q: int = 2
del p
del q
p = 11
q = 22
print(p)
print(q)
