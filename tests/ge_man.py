# Test manual loop equivalent of filtered genexpr (no genexpr syntax)
numbers: list = [1, 2, 3, 4, 5]
filtered: list = []
for x in numbers:
    if x > 3:
        filtered.append(x)
for item in filtered:
    print(item)
