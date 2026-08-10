# Test list methods migrated to Python PIR (count, extend)
# list.count
x: list = [1, 2, 2, 3, 2, 4]
print(x.count(2))
print(x.count(1))
print(x.count(99))
print(x.count(4))
# count on empty list
empty: list = []
print(empty.count(1))
# list.extend
y: list = [1, 2]
y.extend([3, 4, 5])
print(len(y))
for v in y:
    print(v)
# extend with empty
y.extend([])
print(len(y))
# extend on empty list
z: list = []
z.extend([10, 20])
print(len(z))
