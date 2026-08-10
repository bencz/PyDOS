"""Generator tests with various types and patterns."""

def count_up(n: int) -> object:
    i: int = 0
    while i < n:
        yield i
        i = i + 1


def fibonacci(limit: int) -> object:
    a: int = 0
    b: int = 1
    while a < limit:
        yield a
        temp: int = a + b
        a = b
        b = temp


def repeat_str(s: str, n: int) -> object:
    i: int = 0
    while i < n:
        yield s
        i = i + 1


def squares(n: int) -> object:
    i: int = 0
    while i < n:
        yield i * i
        i = i + 1


def filter_positive(items: list) -> object:
    i: int = 0
    while i < len(items):
        val: int = items[i]
        if val > 0:
            yield val
        i = i + 1


def enumerate_list(items: list) -> object:
    idx: int = 0
    while idx < len(items):
        print(idx)
        print(items[idx])
        idx = idx + 1
        yield idx


def main() -> None:
    x: object

    # Test 1: Simple for-in over generator
    for x in count_up(3):
        print(x)

    # Test 2: Generator with more values
    for x in count_up(5):
        print(x)

    # Test 3: Fibonacci sequence
    for x in fibonacci(50):
        print(x)

    # Test 4: Generator yielding strings
    for x in repeat_str("hi", 3):
        print(x)

    # Test 5: Computed values (squares)
    for x in squares(5):
        print(x)

    # Test 6: Generator with conditional yield (filter)
    nums: list = [-2, 5, -1, 3, 0, 7, -4, 1]
    for x in filter_positive(nums):
        print(x)

    # Test 7: Generator with multiple locals and side effects
    items: list = ["a", "b", "c"]
    for x in enumerate_list(items):
        pass


main()
