def add(a: int, b: int) -> int:
    return a + b

def factorial(n: int) -> int:
    result: int = 1
    i: int = 2
    while i <= n:
        result = result * i
        i = i + 1
    return result

x: int = add(3, 4)
print(x)

y: int = factorial(5)
print(y)

z: int = 10 + 20 * 3 - 5
print(z)

if z > 50:
    print("z is greater than 50")
else:
    print("z is 50 or less")

i: int = 0
while i < 5:
    print(i)
    i = i + 1
