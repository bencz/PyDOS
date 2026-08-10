# Test: Multiple inheritance
# Tests class inheriting from two parent classes

class Flyable:
    def fly(self) -> str:
        return "flying"

class Swimmable:
    def swim(self) -> str:
        return "swimming"

class Duck(Flyable, Swimmable):
    def quack(self) -> str:
        return "quack"

def test_multiple_inheritance() -> None:
    d: Duck = Duck()
    print(d.quack())
    print(d.fly())
    print(d.swim())

test_multiple_inheritance()
