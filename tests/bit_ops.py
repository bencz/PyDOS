def test_and(a: int, b: int) -> int:
    return a & b

def test_or(a: int, b: int) -> int:
    return a | b

def test_xor(a: int, b: int) -> int:
    return a ^ b

def test_lshift(a: int, n: int) -> int:
    return a << n

def test_rshift(a: int, n: int) -> int:
    return a >> n

print(test_and(0xFF, 0x0F))
print(test_or(0xF0, 0x0F))
print(test_xor(0xFF, 0x0F))
print(test_lshift(1, 4))
print(test_rshift(256, 4))
