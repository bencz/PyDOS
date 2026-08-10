nums: list = [10, 20, 30, 40, 50]

v: object = nums.pop()
print(v)
print(len(nums))

v2: object = nums.pop(0)
print(v2)
print(len(nums))

nums.insert(0, 99)
print(nums[0])
print(len(nums))

nums.insert(2, 77)
print(nums[2])
print(len(nums))
