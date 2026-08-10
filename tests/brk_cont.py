def find_first_gt(threshold: int) -> int:
    i: int = 0
    while i < 100:
        if i > threshold:
            return i
        i = i + 1
    return -1

def sum_odd(n: int) -> int:
    total: int = 0
    i: int = 0
    while i < n:
        i = i + 1
        if i % 2 == 0:
            continue
        total = total + i
    return total

print(find_first_gt(50))
print(sum_odd(10))
