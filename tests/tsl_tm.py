# Test tuple methods migrated to Python PIR (count, index)
# tuple.count
t: tuple = (1, 2, 3, 2, 1)
print(t.count(1))
print(t.count(2))
print(t.count(3))
print(t.count(99))
# count on empty tuple
empty: tuple = ()
print(empty.count(1))
# tuple.index
t2: tuple = (10, 20, 30, 40)
print(t2.index(10))
print(t2.index(30))
print(t2.index(40))
