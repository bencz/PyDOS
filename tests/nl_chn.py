# Test: closure counter pattern (nonlocal with returned inner function)
# Verifies that cell objects persist after the outer function returns,
# and that repeated calls to the inner function share the same cell.

def make_counter(start: int) -> object:
    count: int = start
    def increment() -> int:
        nonlocal count
        count = count + 1
        return count
    return increment

c: object = make_counter(0)
print(c())
print(c())
print(c())
