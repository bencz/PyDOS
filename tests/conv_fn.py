# conv_fn.py - Conversion builtins: str(), int(), bool(), hex()

# str() from int
print(str(42))
print(str(-7))
print(str(0))

# int() from string
print(int("123"))
print(int("-5"))
print(int("0"))

# bool() from int
print(bool(1))
print(bool(0))
print(bool(-1))

# hex()
print(hex(255))
print(hex(0))
print(hex(16))

# combined
x: int = int("50")
s: str = str(x + 10)
print(s)

print("done")
