# del statement: subscript deletion (regression test)

# del from dict
d: dict = {"a": 1, "b": 2, "c": 3}
print(len(d))
del d["b"]
print(len(d))
print(d["a"])
print(d["c"])

# del from list
lst: list = [10, 20, 30, 40]
print(len(lst))
del lst[1]
print(len(lst))
print(lst[0])
print(lst[1])
print(lst[2])

try:
    del d["missing"]
except KeyError:
    print("missing-key")

try:
    del lst[99]
except IndexError:
    print("missing-index")

class Deletable:
    def __init__(self):
        self.deleted: int = -1

    def __delitem__(self, key: int) -> None:
        self.deleted = key

item: Deletable = Deletable()
del item[7]
print(item.deleted)
