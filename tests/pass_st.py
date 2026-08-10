def do_nothing() -> None:
    pass

def pass_in_if(x: int) -> None:
    if x > 0:
        pass
    else:
        print("not positive")

def pass_in_else(x: int) -> None:
    if x > 0:
        print("positive")
    else:
        pass

def pass_in_while() -> None:
    i: int = 0
    while i < 5:
        pass
        i = i + 1
    print(i)

def pass_in_for() -> None:
    count: int = 0
    i: int = 0
    for i in range(4):
        pass
        count = count + 1
    print(count)

def pass_with_statements() -> None:
    x: int = 10
    pass
    y: int = 20
    pass
    print(x + y)

do_nothing()
print("after do_nothing")

pass_in_if(5)
pass_in_if(-1)

pass_in_else(3)
pass_in_else(-2)

pass_in_while()

pass_in_for()

pass_with_statements()

print("done")
