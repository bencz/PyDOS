def in_range(x: int, lo: int, hi: int) -> bool:
    return lo <= x and x < hi

def max3(a: int, b: int, c: int) -> int:
    if a >= b and a >= c:
        return a
    elif b >= c:
        return b
    else:
        return c

print(in_range(5, 1, 10))
print(in_range(15, 1, 10))
print(max3(3, 7, 5))
print(max3(9, 2, 4))
print(max3(1, 1, 8))

print(10 == 10)
print(10 != 10)
print(5 < 3)
print(5 > 3)
