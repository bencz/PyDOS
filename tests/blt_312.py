# Builtins added while closing the Python 3.12 core API gap.

print(bin(0))
print(bin(42))
print(bin(-42))
print(oct(0))
print(oct(65))
print(oct(-65))

positive: tuple = divmod(7, 3)
print(positive[0])
print(positive[1])
negative: tuple = divmod(-7, 3)
print(negative[0])
print(negative[1])

print(pow(2, 10))
print(pow(2, -2))
print(pow(2, 10, 1000))
print(pow(3, -1, 11))
print(pow(3, 0, -5))

try:
    pow(2, 3, 0)
except ValueError:
    print("zero-mod")

try:
    pow(2, -1, 4)
except ValueError:
    print("no-inverse")
