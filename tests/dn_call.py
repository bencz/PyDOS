# dn_call.py - Callable object dunder
class Adder:
    def __init__(self, base: int) -> None:
        self.base = base
    def __call__(self, x: int) -> int:
        return self.base + x
    def __str__(self) -> str:
        return "Adder(" + str(self.base) + ")"

add5: Adder = Adder(5)
print(add5(3))
print(add5(10))

class Multiplier:
    def __init__(self, factor: int) -> None:
        self.factor = factor
    def __call__(self, a: int, b: int) -> int:
        return (a + b) * self.factor

mul3: Multiplier = Multiplier(3)
print(mul3(2, 4))
