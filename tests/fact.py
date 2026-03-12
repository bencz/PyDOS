def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

for i in range(1, 10):
    print(factorial(i))
