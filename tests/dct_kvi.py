d: dict = {"a": 1, "b": 2, "c": 3}

ks: list = d.keys()
print(len(ks))
for k in ks:
    print(k)

vs: list = d.values()
print(len(vs))
for v in vs:
    print(v)

its: list = d.items()
print(len(its))
for pair in its:
    print(pair)

empty: dict = {}
print(len(empty.keys()))
print(len(empty.values()))
print(len(empty.items()))
