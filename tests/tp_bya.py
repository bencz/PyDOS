# tp_bya.py - bytearray integration test

# Empty bytearray
ba = bytearray()
print(len(ba))

# bytearray from int (zero-filled)
ba2 = bytearray(5)
print(len(ba2))
print(ba2[0])

# Append
ba3 = bytearray()
ba3.append(65)
ba3.append(66)
ba3.append(67)
print(len(ba3))

# Indexing
print(ba3[0])
print(ba3[1])
print(ba3[2])
print(ba3[-1])

# Pop
val = ba3.pop()
print(val)
print(len(ba3))

# Clear
ba3.clear()
print(len(ba3))

# Bool truthiness
print(bool(bytearray()))
print(bool(bytearray(1)))

# bytearray from list
ba4 = bytearray([72, 101, 108, 108, 111])
print(len(ba4))
print(ba4[0])
print(ba4[4])

# Contains
print(72 in ba4)
print(200 in ba4)
