# blt_ops.py - Builtin functions: abs, min, max, ord, chr

# abs
print(abs(5))
print(abs(-7))
print(abs(0))

# min
print(min(3, 7))
print(min(10, 2))
print(min(-1, -5))

# max
print(max(3, 7))
print(max(10, 2))
print(max(-1, -5))

# ord
print(ord("A"))
print(ord("a"))
print(ord("0"))

# chr
print(chr(65))
print(chr(97))
print(chr(48))

# combined usage
def clamp(val: int, lo: int, hi: int) -> int:
    return min(max(val, lo), hi)

print(clamp(15, 0, 10))
print(clamp(-3, 0, 10))
print(clamp(5, 0, 10))

# ord/chr round-trip
c: str = chr(ord("Z"))
print(c)
