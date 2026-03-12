# Test dict methods migrated to Python PIR (update, copy, popitem)
# dict.update
d: dict = {"a": 1, "b": 2}
d.update({"c": 3, "b": 20})
print(len(d))
print(d["a"])
print(d["b"])
print(d["c"])
# update with empty
d.update({})
print(len(d))
# dict.copy
d2: dict = d.copy()
print(len(d2))
print(d2["a"])
print(d2["b"])
# copy of empty dict
e: dict = {}
e2: dict = e.copy()
print(len(e2))
# dict.popitem
d3: dict = {"x": 10, "y": 20}
t: tuple = d3.popitem()
print(len(d3))
