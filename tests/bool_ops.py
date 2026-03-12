def check_and(a: bool, b: bool) -> bool:
    return a and b

def check_or(a: bool, b: bool) -> bool:
    return a or b

def check_not(a: bool) -> bool:
    return not a

x: bool = True
y: bool = False

print(check_and(x, y))
print(check_and(x, x))
print(check_or(x, y))
print(check_or(y, y))
print(check_not(x))
print(check_not(y))
