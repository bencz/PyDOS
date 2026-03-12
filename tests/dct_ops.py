# dct_ops.py - Dict operations

d: dict[str, int] = {"a": 1, "b": 2, "c": 3}
print(d["a"])
print(d["c"])
print(len(d))

# overwrite
d["a"] = 10
print(d["a"])
print(len(d))

# add new key
d["d"] = 4
print(d["d"])
print(len(d))

print("done")
