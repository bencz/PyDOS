# lst_ops.py - List insert, reverse, pop with index

nums: list[int] = [1, 2, 3]

# insert at beginning
nums.insert(0, 10)
print(nums[0])
print(len(nums))

# insert in middle
nums.insert(2, 20)
print(nums[2])
print(len(nums))

# reverse
nums.reverse()
print(nums[0])
print(nums[4])

# pop with index
x: int = nums.pop(0)
print(x)
print(len(nums))

# pop last
y: int = nums.pop(-1)
print(y)
print(len(nums))

# string list operations
words: list[str] = ["c", "a", "b"]
words.reverse()
print(words[0])
print(words[2])

print("done")
