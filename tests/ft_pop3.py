# ft_pop3.py - Stack[int] with peek/size then Stack[str] with size between pops

class Stack[T]:
    def __init__(self) -> None:
        self.items: list[T] = []
        self.count: int = 0

    def push(self, item: T) -> None:
        self.items.append(item)
        self.count += 1

    def pop(self) -> T:
        if self.count == 0:
            raise ValueError("empty stack")
        self.count -= 1
        return self.items.pop(self.count)

    def peek(self) -> T:
        if self.count == 0:
            raise ValueError("empty stack")
        return self.items[self.count - 1]

    def size(self) -> int:
        return self.count

    def is_empty(self) -> bool:
        return self.count == 0

si: Stack[int] = Stack[int]()
si.push(10)
si.push(20)
si.push(30)
print(si.peek())
print(si.pop())
print(si.size())

ss: Stack[str] = Stack[str]()
ss.push("alpha")
ss.push("beta")
ss.push("gamma")
print(ss.size())
print(ss.pop())
print(ss.size())
print(ss.pop())
print(ss.size())
print(ss.pop())
print(ss.is_empty())
