"""Rich comparison against operands of another type.

Equality bailed out on a type mismatch before reaching __eq__, so an object
could never compare equal to a str or an int.  Ordering only tried __lt__ and
__gt__ on the left operand, so a class defining just __lt__ never answered "a >
b" and sorted() left its input untouched.
"""


class Key:
    def __init__(self, code: int, name: str = "") -> None:
        self.code = code
        self.name = name

    def __eq__(self, other):
        if isinstance(other, str):
            return self.name == other
        if isinstance(other, int):
            return self.code == other
        if isinstance(other, Key):
            return self.code == other.code
        return NotImplemented

    def __ne__(self, other):
        result = self.__eq__(other)
        if result is NotImplemented:
            return result
        return not result

    def __hash__(self):
        return self.code

    def __lt__(self, other):
        return self.code < other.code

    def __str__(self) -> str:
        return "Key(" + str(self.code) + ")"


quit_key = Key(113, "q")

print(quit_key == "q", quit_key == 113, quit_key == "z", quit_key == 999)
print("q" == quit_key, 113 == quit_key, "z" == quit_key)
print(quit_key != "q", quit_key != "z", quit_key != 113)
print(quit_key == Key(113), quit_key != Key(1))
print(quit_key == None, quit_key == [1], quit_key == (1, 2))

bindings = {quit_key: "quit", Key(27, "esc"): "cancel"}
print(bindings[Key(113)], bindings[Key(27)])
print(len({Key(1), Key(1), Key(2)}))
print(Key(1) in [Key(2), Key(1)])
print(Key(5) in {Key(5): "x"})

print(Key(1) < Key(2), Key(2) < Key(1))
print(Key(1) > Key(2), Key(2) > Key(1))

unordered = [Key(3), Key(1), Key(2)]
codes = []
for item in sorted(unordered):
    codes.append(item.code)
print(codes)

reversed_codes = []
for item in sorted(unordered, reverse=True):
    reversed_codes.append(item.code)
print(reversed_codes)

by_name = []
for item in sorted([Key(2, "b"), Key(1, "a")], key=lambda k: k.name):
    by_name.append(item.name)
print(by_name)

print(str(max(unordered)), str(min(unordered)))


class Version:
    def __init__(self, major: int, minor: int) -> None:
        self.major = major
        self.minor = minor

    def __eq__(self, other):
        if isinstance(other, str):
            return str(self.major) + "." + str(self.minor) == other
        if isinstance(other, Version):
            return self.major == other.major and self.minor == other.minor
        return NotImplemented

    def __hash__(self):
        return self.major * 100 + self.minor

    def __lt__(self, other):
        if self.major != other.major:
            return self.major < other.major
        return self.minor < other.minor


print(Version(3, 12) == "3.12", Version(3, 12) == "3.11")
print(Version(3, 12) == Version(3, 12), Version(3, 12) == Version(3, 11))
print(Version(3, 11) < Version(3, 12), Version(4, 0) > Version(3, 12))

versions = [Version(3, 12), Version(2, 7), Version(3, 11)]
ordered = []
for item in sorted(versions):
    ordered.append(str(item.major) + "." + str(item.minor))
print(ordered)


class Always:
    def __eq__(self, other):
        return True

    def __hash__(self):
        return 0


always = Always()
print(always == 1, always == "text", always == None, always == [0])
print(1 == always, "text" == always)


class Never:
    def __eq__(self, other):
        return NotImplemented

    def __hash__(self):
        return 1


never = Never()
print(never == 1, never == never, never == Never())
