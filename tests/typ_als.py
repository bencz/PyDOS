# Basic type aliases
type IntList = list[int]
type Number = int

# Usage in annotations
x: Number = 42
print(x)

nums: IntList = [1, 2, 3]
print(len(nums))

# Type alias with union
type NumOrStr = int | str
y: NumOrStr = "hello"
print(y)

# Type alias inside function
def test_local_alias() -> None:
    type LocalInt = int
    a: LocalInt = 99
    print(a)

test_local_alias()

# Generic type alias (syntax accepted, params not enforced)
type Pair[T] = list[T]
p: list[int] = [10, 20]
print(len(p))

print("done")
