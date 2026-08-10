# ft_pop.py - Minimal test to isolate Stack[str] pop bug

class Stack[T]:
    def __init__(self) -> None:
        self.items: list[T] = []
        self.count: int = 0

    def push(self, item: T) -> None:
        self.items.append(item)
        self.count = self.count + 1

    def pop(self) -> T:
        self.count = self.count - 1
        return self.items.pop(self.count)

    def size(self) -> int:
        return self.count

ss: Stack[str] = Stack[str]()
ss.push("A")
ss.push("B")
ss.push("C")
print(ss.pop())
print(ss.pop())
print(ss.pop())
