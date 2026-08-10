x: int = 10

def double(n: int) -> int:
    return n * 2

class Box:
    def __init__(self: "Box", val: int) -> None:
        self.val: int = val
    def get(self: "Box") -> int:
        return self.val
    def __str__(self: "Box") -> str:
        return "Box(" + str(self.val) + ")"

def main_test() -> None:
    print(double(x))
    print(double(21))
    b: Box = Box(42)
    print(b.get())
    print(str(b))

main_test()
