# Test: boxed integer comparison (isolates pydos_obj_compare_ bug)
# Expected output: 4\n5\n
nums: list = [1, 2, 3, 4, 5]
for x in nums:
    if x > 3:
        print(x)
