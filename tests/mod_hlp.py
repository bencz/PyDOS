def add(a: int, b: int) -> int:
    return a + b

def greet(name: str) -> str:
    return "Hello " + name

class Counter:
    def __init__(self: "Counter", start: int) -> None:
        self.value: int = start
    def inc(self: "Counter") -> None:
        self.value = self.value + 1
    def get(self: "Counter") -> int:
        return self.value
