def sum_range(n: int) -> int:
    total: int = 0
    i: int = 0
    for i in range(n):
        total = total + i
    return total

print(sum_range(5))
print(sum_range(10))
