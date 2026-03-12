# Test min/max builtins (Python-backed via stdlib PIR)
# Integers
print(min(3, 7))
print(min(10, 2))
print(min(-1, -5))
print(max(3, 7))
print(max(10, 2))
print(max(-1, -5))
# Same values
print(min(5, 5))
print(max(5, 5))
# Strings
print(min("apple", "banana"))
print(max("apple", "banana"))
# Mixed in expression
x: int = min(max(3, 1), 5)
print(x)
