# Test dict comprehension
numbers: list = [1, 2, 3, 4, 5]

# Basic dict comprehension
squares: dict = {x: x * x for x in numbers}
print(squares[1])
print(squares[3])
print(squares[5])

# Dict comprehension with filter
even_sq: dict = {x: x * x for x in numbers if x % 2 == 0}
print(even_sq[2])
print(even_sq[4])
