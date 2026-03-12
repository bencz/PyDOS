x: int = 42

def add(a: int, b: int) -> int:
    return a + b

class Greeter:
    def __init__(self: "Greeter", name: str) -> None:
        self.name: str = name
    def hello(self: "Greeter") -> str:
        return "Hello " + self.name

def main() -> None:
    print(x)
    print(add(10, 20))
    g: Greeter = Greeter("DOS")
    print(g.hello())
    print(add(x, 8))
