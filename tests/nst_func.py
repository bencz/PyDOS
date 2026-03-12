def double(n: int) -> int:
    return n + n

def quadruple(n: int) -> int:
    return double(double(n))

def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

print(double(5))
print(quadruple(3))
print(factorial(6))
