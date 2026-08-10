# bytes primitive and str.encode compatibility

empty: bytes = bytes()
print(isinstance(empty, bytes))
print(len(empty))
print(bool(empty))
print(str(empty))

ascii_value: bytes = "Hello".encode()
print(isinstance(ascii_value, bytes))
print(len(ascii_value))
print(ascii_value[0])
print(ascii_value[-1])
print(str(ascii_value))
print(72 in ascii_value)
print(99 in ascii_value)

slice_value: bytes = ascii_value[1:4]
print(str(slice_value))
print(str(ascii_value + "!".encode()))
print(str("ab".encode() * 3))

raw: bytes = bytes([0, 65, 255])
print(len(raw))
print(raw[0])
print(raw[1])
print(raw[2])
print(str(raw))
print(raw == bytes([0, 65, 255]))
print(hash(raw) == hash(bytes([0, 65, 255])))

literal: bytes = b"A\x00\xff"
print(isinstance(literal, bytes))
print(len(literal))
print(literal[0])
print(literal[1])
print(literal[2])
print(str(literal))

utf8_value: bytes = chr(200).encode("utf-8")
print(len(utf8_value))
print(utf8_value[0])
print(utf8_value[1])

latin1_value: bytes = chr(200).encode("latin-1")
print(len(latin1_value))
print(latin1_value[0])
print(utf8_value.decode("utf-8") == chr(200))
print(latin1_value.decode("latin-1") == chr(200))
print(ascii_value.decode())
print(raw.hex())
print(raw.hex(":"))

ignored: bytes = ("A" + chr(200) + "B").encode("ascii", "ignore")
replaced: bytes = ("A" + chr(200) + "B").encode("ascii", "replace")
print(str(ignored))
print(str(replaced))

try:
    chr(200).encode("ascii")
except UnicodeEncodeError:
    print("ascii-error")

try:
    "abc".encode("unknown")
except LookupError:
    print("lookup-error")

def dynamic_encode(value) -> bytes:
    return value.encode("utf-8")

def dynamic_decode(value) -> str:
    return value.decode("utf-8")

dynamic_value: bytes = dynamic_encode("DOS")
print(isinstance(dynamic_value, bytes))
print(str(dynamic_value))
print(dynamic_decode(dynamic_value))

try:
    print(ascii_value[99])
except IndexError:
    print("bytes-index-error")
