# Test list.remove and list.copy (PIR-backed)
a = [1, 2, 3, 4, 5]
a.remove(3)
print(a)

# copy is independent
b = a.copy()
b.append(99)
print(a)
print(b)

# remove first occurrence only
c = [1, 2, 1, 3]
c.remove(1)
print(c)

# remove raises ValueError
try:
    d = [1, 2, 3]
    d.remove(99)
except ValueError:
    print("caught ValueError")
