class Stack[T]:
    def __init__(self) -> None:
        self.items: list[T] = []
        self.size: int = 0

    def push(self, item: T) -> None:
        self.items.append(item)
        self.size = self.size + 1

    def pop(self) -> T:
        if self.size == 0:
            raise ValueError("Stack is empty")
        self.size = self.size - 1
        return self.items.pop(self.size)

    def peek(self) -> T:
        if self.size == 0:
            raise ValueError("Stack is empty")
        return self.items[self.size - 1]

    def is_empty(self) -> bool:
        return self.size == 0

int_stack: Stack[int] = Stack[int]()
int_stack.push(10)
int_stack.push(20)
int_stack.push(30)

print(int_stack.peek())
print(int_stack.pop())
print(int_stack.pop())
print(int_stack.is_empty())

str_stack: Stack[str] = Stack[str]()
str_stack.push("hello")
str_stack.push("world")
print(str_stack.pop())
print(str_stack.pop())
print(str_stack.is_empty())
