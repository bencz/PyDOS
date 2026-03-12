# lst_itr.py - for-over-list and 'in' operator

def sum_list(items: list[int]) -> int:
    total: int = 0
    x: int = 0
    for x in items:
        total = total + x
    return total

nums: list[int] = [10, 20, 30]
print(sum_list(nums))

# for over string list
names: list[str] = ["hello", "world", "dos"]
s: str = ""
for s in names:
    print(s)

# in operator
vals: list[int] = [1, 2, 3, 4, 5]
if 3 in vals:
    print("found 3")

if 10 in vals:
    print("found 10")
else:
    print("no 10")

# not in
if 99 not in vals:
    print("no 99")

# in with strings
words: list[str] = ["cat", "dog", "fish"]
if "dog" in words:
    print("found dog")

if "bird" in words:
    print("found bird")
else:
    print("no bird")

# empty list iteration
empty: list[int] = []
n: int = 0
for n in empty:
    print(n)
print("after empty")
