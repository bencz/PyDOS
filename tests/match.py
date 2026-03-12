# Test match/case statement
def describe(val) -> str:
    match val:
        case 1:
            return "one"
        case 2:
            return "two"
        case 3:
            return "three"
        case _:
            return "other"

print(describe(1))
print(describe(2))
print(describe(3))
print(describe(42))

# Match with string literals
def greet(lang: str) -> str:
    match lang:
        case "en":
            return "Hello"
        case "es":
            return "Hola"
        case "fr":
            return "Bonjour"
        case _:
            return "Hi"

print(greet("en"))
print(greet("es"))
print(greet("de"))

# Match with capture
def check(x) -> None:
    match x:
        case 0:
            print("zero")
        case n:
            print(n)

check(0)
check(99)
