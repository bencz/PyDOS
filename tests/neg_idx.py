# neg_idx.py - Negative list indices

nums: list[int] = [10, 20, 30, 40, 50]

# basic negative indexing
print(nums[-1])
print(nums[-2])
print(nums[-5])

# negative index in expressions
total: int = nums[-1] + nums[-2]
print(total)

# negative index after append
nums.append(60)
print(nums[-1])

# negative index with strings
words: list[str] = ["alpha", "beta", "gamma"]
print(words[-1])
print(words[-3])

print("done")
