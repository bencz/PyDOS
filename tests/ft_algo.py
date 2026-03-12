# ft_algo.py - Algorithms, bitwise, augmented assign, boolean, strings

def is_prime(n: int) -> bool:
    if n < 2:
        return False
    i: int = 2
    while i * i <= n:
        if n % i == 0:
            return False
        i += 1
    return True

def count_primes(limit: int) -> int:
    count: int = 0
    n: int = 2
    while n < limit:
        if is_prime(n):
            count += 1
        n += 1
    return count

def fib(n: int) -> int:
    if n <= 1:
        return n
    return fib(n - 1) + fib(n - 2)

def gcd(a: int, b: int) -> int:
    while b != 0:
        t: int = b
        b = a % b
        a = t
    return a

def collatz(n: int) -> int:
    steps: int = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = n * 3 + 1
        steps += 1
    return steps

def int_to_bin(n: int) -> str:
    if n == 0:
        return "0"
    result: str = ""
    while n > 0:
        if n % 2 == 0:
            result = "0" + result
        else:
            result = "1" + result
        n = n // 2
    return result

print(count_primes(50))
print(fib(10))
print(gcd(48, 18))
print(collatz(27))
print(int_to_bin(42))
print(int_to_bin(255))

print(0xFF & 0x0F)
print(0xF0 | 0x0F)
print(1 << 10)
print(1024 >> 5)

x: int = 10
x += 5
x *= 4
x -= 10
x //= 5
print(x)

def xor(a: bool, b: bool) -> bool:
    return (a or b) and not (a and b)

print(xor(True, False))
print(xor(False, False))
print(not False and True)

inventory: dict[str, int] = {"sword": 1, "potion": 5, "gold": 100}
print(len(inventory))
print(inventory["potion"])
print(inventory["gold"])

bag: list[int] = [1, 2, 3, 4, 5]
bag.append(6)
bag.append(7)

def sum_list(lst: list[int]) -> int:
    total: int = 0
    i: int = 0
    for i in range(len(lst)):
        total += lst[i]
    return total

print(sum_list(bag))

def greet(name: str) -> str:
    return "Hello, " + name + "!"

print(greet("DOS"))
print(len(greet("DOS")))

s1: str = "abc"
s2: str = "abc"
s3: str = "xyz"
print(s1 == s2)
print(s1 == s3)
