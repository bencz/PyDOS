# Test generator expressions (eagerly materialized)
numbers: list = [1, 2, 3, 4, 5]

# Generator expression passed to list()
result: list = list(x * 2 for x in numbers)
for item in result:
    print(item)

# Generator expression with filter
filtered: list = list(x for x in numbers if x > 3)
for item in filtered:
    print(item)
