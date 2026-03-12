# is_sub.py - issubclass() tests

# Test 1: same type
def test_same_type() -> None:
    print(issubclass(int, int))
    print(issubclass(str, str))
    print(issubclass(bool, bool))
    print(issubclass(list, list))
    print(issubclass(dict, dict))

# Test 2: bool is subclass of int
def test_bool_int() -> None:
    print(issubclass(bool, int))

# Test 3: unrelated builtin types
def test_unrelated() -> None:
    print(issubclass(int, str))
    print(issubclass(str, int))
    print(issubclass(list, dict))
    print(issubclass(int, bool))

# Test 4: user-defined classes
class Animal:
    def __init__(self, name: str) -> None:
        self.name: str = name

class Dog(Animal):
    def __init__(self, name: str) -> None:
        super().__init__(name)

class Cat(Animal):
    def __init__(self, name: str) -> None:
        super().__init__(name)

def test_user_classes() -> None:
    print(issubclass(Dog, Animal))
    print(issubclass(Cat, Animal))
    print(issubclass(Dog, Dog))
    print(issubclass(Animal, Animal))
    print(issubclass(Animal, Dog))

# Test 5: three-level hierarchy
class Shape:
    def __init__(self) -> None:
        self.kind: str = "shape"

class Rectangle(Shape):
    def __init__(self) -> None:
        super().__init__()

class Square(Rectangle):
    def __init__(self) -> None:
        super().__init__()

def test_deep_hierarchy() -> None:
    print(issubclass(Square, Rectangle))
    print(issubclass(Square, Shape))
    print(issubclass(Rectangle, Shape))
    print(issubclass(Shape, Square))

# Test 6: issubclass in conditionals
def test_conditional() -> None:
    if issubclass(Dog, Animal):
        print("Dog is subclass of Animal")
    else:
        print("WRONG")
    if issubclass(int, str):
        print("WRONG")
    else:
        print("int is not subclass of str")

def main() -> None:
    test_same_type()
    test_bool_int()
    test_unrelated()
    test_user_classes()
    test_deep_hierarchy()
    test_conditional()
    print("all issubclass tests done")

main()
