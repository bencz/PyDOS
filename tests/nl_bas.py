# Test: basic nonlocal variable mutation
# Verifies that inner functions can read and modify outer scope variables
# via cell objects (PYDT_CELL closure mechanism).

def outer() -> None:
    x: int = 10
    def inner() -> None:
        nonlocal x
        x = x + 1
    inner()
    print(x)
    inner()
    print(x)

outer()
