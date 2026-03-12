"""First-class function tests with complex types."""

class Counter:
    def __init__(self, start: int) -> None:
        self.val: int = start

    def __str__(self) -> str:
        return str(self.val)

    def increment(self) -> None:
        self.val = self.val + 1


def apply(f: object, x: int) -> int:
    return f(x)


def double(x: int) -> int:
    return x * 2


def format_item(item: object) -> str:
    return str(item)


def apply_to_list(f: object, items: list) -> list:
    result: list = []
    i: int = 0
    while i < len(items):
        result.append(f(items[i]))
        i = i + 1
    return result


def greet(name: str) -> str:
    return "Hello " + name


def main() -> None:
    # Test 1: Lambda stored in variable, called later
    sq: object = lambda x: x * x
    print(sq(5))

    # Test 2: Lambda passed as argument
    print(apply(lambda x: x + 10, 7))

    # Test 3: Multiple lambdas
    add1: object = lambda x: x + 1
    sub1: object = lambda x: x - 1
    print(add1(10))
    print(sub1(10))

    # Test 4: Named function as first-class value
    fn: object = double
    print(fn(21))

    # Test 5: String processing with first-class functions
    greeter: object = greet
    print(greeter("World"))

    # Test 6: Apply named function to list
    nums: list = [1, 2, 3, 4, 5]
    doubled: list = apply_to_list(double, nums)
    i: int = 0
    while i < len(doubled):
        print(doubled[i])
        i = i + 1

    # Test 7: Apply lambda to list
    tripled: list = apply_to_list(lambda x: x * 3, nums)
    i = 0
    while i < len(tripled):
        print(tripled[i])
        i = i + 1

    # Test 8: Function stored in dict
    ops: dict = {}
    ops["dbl"] = double
    ops["fmt"] = format_item
    f: object = ops["dbl"]
    print(f(50))

    # Test 9: Class method interaction
    c: Counter = Counter(10)
    print(c)
    c.increment()
    c.increment()
    c.increment()
    print(c)


main()
