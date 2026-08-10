# Test: List comprehensions
# Tests basic list comprehension with filtering

def test_listcomp() -> None:
    # Basic list comprehension
    nums: list = [x for x in range(5)]
    print(nums)

    # With filter condition
    evens: list = [x for x in range(10) if x % 2 == 0]
    print(evens)

    # With arithmetic
    doubled: list = [x + x for x in range(4)]
    print(doubled)

test_listcomp()
