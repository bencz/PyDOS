# dn_cont.py - Dunder container tests
# Tests: __len__, __getitem__, __contains__

class Bag:
    def __init__(self) -> None:
        self.items: list = []

    def add(self, item: int) -> None:
        self.items.append(item)

    def __len__(self) -> int:
        return len(self.items)

    def __getitem__(self, idx: int) -> int:
        return self.items[idx]

    def __contains__(self, item: int) -> bool:
        for x in self.items:
            if x == item:
                return True
        return False

b: Bag = Bag()
b.add(10)
b.add(20)
b.add(30)

print(len(b))
print(b[0])
print(b[2])
