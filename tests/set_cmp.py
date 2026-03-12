# Test set comprehension
numbers: list = [1, 2, 2, 3, 3, 3]

# Basic set comprehension (should deduplicate)
unique: set = {x for x in numbers}
print(len(unique))

# Set comprehension with filter
evens: set = {x for x in [1, 2, 3, 4, 5, 6] if x % 2 == 0}
print(len(evens))
