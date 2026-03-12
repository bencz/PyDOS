# dn_bool.py - Dunder bool/str/repr tests
# Tests: __bool__, __str__, __repr__

class Maybe:
    def __init__(self, val: int) -> None:
        self.val = val

    def __bool__(self) -> bool:
        return self.val != 0

    def __str__(self) -> str:
        return "Maybe(" + str(self.val) + ")"

    def __repr__(self) -> str:
        return "Maybe(val=" + str(self.val) + ")"

a: Maybe = Maybe(42)
b: Maybe = Maybe(0)

print(str(a))
print(str(b))

if a:
    print("a is truthy")
if not b:
    print("b is falsy")
