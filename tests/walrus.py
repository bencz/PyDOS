# Test walrus operator :=
items: list = [1, 2, 3, 4, 5]

# Basic walrus in if
x: int = 0
if (n := 10) > 5:
    print(n)

# Walrus in while
data: list = [1, 2, 3]
i: int = 0
while (val := i) < 3:
    print(val)
    i = i + 1
