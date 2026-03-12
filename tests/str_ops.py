def greet(name: str) -> str:
    return "Hello, " + name + "!"

msg: str = greet("World")
print(msg)
print(len(msg))

a: str = "abc"
b: str = "abc"
c: str = "xyz"
print(a == b)
print(a == c)
