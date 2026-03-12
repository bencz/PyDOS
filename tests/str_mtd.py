# Test string methods
s: str = "Hello World"

# Case transformations
print(s.upper())
print(s.lower())
print(s.title())
print(s.capitalize())
print(s.swapcase())

# Strip
padded: str = "  hello  "
print("[" + padded.strip() + "]")
print("[" + padded.lstrip() + "]")
print("[" + padded.rstrip() + "]")

# Search
print(s.find("World"))
print(s.find("xyz"))
print(s.rfind("l"))
print(s.count("l"))

# Predicates
print(s.startswith("Hello"))
print(s.endswith("World"))
print("123".isdigit())
print("abc".isalpha())
print("abc123".isalnum())
print("   ".isspace())
print("ABC".isupper())
print("abc".islower())

# Split/join
words: list = "a,b,c".split(",")
print(",".join(words))
lines: list = "hello world".split(" ")
print(lines[0])
print(lines[1])

# Replace
print("aabbcc".replace("bb", "XX"))

# Padding (brackets to show exact width)
print("[" + "hi".center(6) + "]")
print("[" + "hi".ljust(6) + "]")
print("[" + "hi".rjust(6) + "]")
print("42".zfill(5))
