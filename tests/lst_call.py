"""Regression: list/tuple/dict/set with function call elements.

Ensures push_arg for collection elements does not interleave
with push_arg for sub-expression function calls.
"""

def double(x: int) -> int:
    return x * 2

def triple(x: int) -> int:
    return x * 3

# List with function call elements
nums: list = [double(5), triple(4)]
print(nums[0])
print(nums[1])

# Tuple with function call elements
t: tuple = (double(3), triple(2))
print(t[0])
print(t[1])

# Dict with function call keys/values
d: dict = {double(1): triple(10)}
print(d[2])

# Set with function call elements
s: set = {double(7), triple(3)}
print(14 in s)
print(9 in s)

# Nested: list of function calls with multiple args
def add(a: int, b: int) -> int:
    return a + b

result: list = [add(1, 2), add(3, 4), add(5, 6)]
print(result[0])
print(result[1])
print(result[2])
