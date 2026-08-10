# Test filtered generator expression in isolation (no preceding genexpr)
numbers: list = [1, 2, 3, 4, 5]
filtered: list = list(x for x in numbers if x > 3)
for item in filtered:
    print(item)
