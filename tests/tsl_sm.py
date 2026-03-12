# Test set methods migrated to Python PIR
# set.update
s: set = {1, 2}
s.update({3, 4})
print(len(s))
# set.copy
s2: set = s.copy()
print(len(s2))
print(1 in s2)
print(3 in s2)
# set.issubset
print({1, 2}.issubset({1, 2, 3}))
print({1, 4}.issubset({1, 2, 3}))
# set.issuperset
print({1, 2, 3}.issuperset({1, 2}))
print({1, 2}.issuperset({1, 2, 3}))
# set.isdisjoint
print({1, 2}.isdisjoint({3, 4}))
print({1, 2}.isdisjoint({2, 3}))
# set.union
u: set = {1, 2, 3}.union({3, 4, 5})
print(len(u))
# set.intersection
i: set = {1, 2, 3}.intersection({2, 3, 4})
print(len(i))
# set.difference
d: set = {1, 2, 3}.difference({2, 3, 4})
print(len(d))
# set.symmetric_difference
sd: set = {1, 2, 3}.symmetric_difference({3, 4, 5})
print(len(sd))
# update with empty
s3: set = {1, 2}
s3.update({})
print(len(s3))
# copy of empty set
e: set = {0}
e.clear()
e2: set = e.copy()
print(len(e2))
# issubset of empty
print(e.issubset({1, 2}))
