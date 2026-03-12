# Test genexpr with print checkpoints to isolate failure point
numbers: list = [1, 2, 3, 4, 5]
result: list = list(x * 2 for x in numbers)
for item in result:
    print(item)
print(0)
filtered: list = list(x for x in numbers if x > 3)
print(len(filtered))
for item in filtered:
    print(item)
print(99)
