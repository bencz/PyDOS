# Python 3.12 string stdlib behavior and primitive-vtable dispatch

parts: list = "  a  b c  ".split()
print(len(parts))
print(parts[0])
print(parts[1])
print(parts[2])

left: list = "a,b,c".split(",", 1)
print(left[0])
print(left[1])

right: list = "a,b,c".rsplit(",", 1)
print(right[0])
print(right[1])

ws_left: list = "  a  b  ".split(None, 1)
print(ws_left[0])
print(ws_left[1])

ws_right: list = "  a  b  ".rsplit(None, 1)
print(ws_right[0])
print(ws_right[1])
print(len("   ".split()))

try:
    "abc".split("")
except ValueError:
    print("empty-separator")

lines: list = "a\r\nb\nc\r".splitlines()
print(len(lines))
print(lines[0])
print(lines[1])
print(lines[2])
print("".join("a\r\nb\nc\r".splitlines(True)) == "a\r\nb\nc\r")

print("[" + "hi".center(6, "-") + "]")
print("[" + "hi".center(5, "-") + "]")
print("[" + "a".center(4, "-") + "]")
print("[" + "abc".center(4, "-") + "]")
print("[" + "hi".ljust(6, ".") + "]")
print("[" + "hi".rjust(6, ".") + "]")
try:
    "hi".center(6, "xx")
except TypeError:
    print("fill-error")

print("HeLLo".casefold())
print("prevalue".removeprefix("pre"))
print("value.txt".removesuffix(".txt"))
print("xyxvaluexy".strip("xy"))
print("xyxvaluexy".lstrip("xy"))
print("xyxvaluexy".rstrip("xy"))
print("aaaa".replace("a", "b", 2))
print("abc".replace("", "-", 2))
print("aaaa".replace("a", "b", 0))

print("abracadabra".find("a", 1, 8))
print("abracadabra".rfind("a", 1, 8))
print("abracadabra".count("a", 1, 8))
print("abracadabra".index("a", 1, 8))
print("abracadabra".rindex("a", 1, 8))
print("abracadabra".find("a", -4))
print("abracadabra".startswith("bra", 1, 4))
print("abracadabra".endswith("bra", 0, 4))
print("abc".find("", 4))
print("abc".rfind("", 0, 2))
print("abc".count("", 1, 2))

p: tuple = "a=b=c".partition("=")
print(p[0])
print(p[1])
print(p[2])
rp: tuple = "a=b=c".rpartition("=")
print(rp[0])
print(rp[1])
print(rp[2])
missing: tuple = "abc".partition(":")
print(missing[0])
print("[" + missing[1] + "]")
print("[" + missing[2] + "]")

print("ASCII".isascii())
print(chr(200).isascii())
print("123".isdecimal())
print("123".isnumeric())
print("_name2".isidentifier())
print("2name".isidentifier())
print("abc !".isprintable())
print("a\n".isprintable())
print("".isprintable())
print("Hello World".istitle())
print("Hello world".istitle())
print("[" + "a\tb".expandtabs(4) + "]")
print("a\tb\n\tc".expandtabs(4) == "a   b\n    c")

def dynamic_remove(value) -> str:
    return value.removeprefix("pre")

def dynamic_rsplit(value) -> list:
    return value.rsplit(",", 1)

def dynamic_center(value) -> str:
    return value.center(5)

def dynamic_find(value) -> int:
    return value.find("a", 1, 8)

def dynamic_replace(value) -> str:
    return value.replace("a", "b", 2)

print(dynamic_remove("prevalue"))
dynamic_parts: list = dynamic_rsplit("a,b,c")
print(dynamic_parts[0])
print(dynamic_parts[1])
print("[" + dynamic_center("hi") + "]")
print(dynamic_find("abracadabra"))
print(dynamic_replace("aaaa"))
print(hasattr("prevalue", "removeprefix"))
