# Test two generator expressions over same list, no filter
numbers: list = [1, 2, 3, 4, 5]
result: list = list(x * 2 for x in numbers)
for item in result:
    print(item)
result2: list = list(x + 10 for x in numbers)
for item in result2:
    print(item)
