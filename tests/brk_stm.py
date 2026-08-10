def break_while() -> int:
    i: int = 0
    while i < 10:
        if i == 5:
            break
        i = i + 1
    return i

def break_for() -> int:
    result: int = -1
    i: int = 0
    for i in range(20):
        if i == 7:
            result = i
            break
    return result

def nested_break() -> None:
    outer: int = 0
    while outer < 3:
        inner: int = 0
        while inner < 10:
            if inner == 2:
                break
            inner = inner + 1
        print(inner)
        outer = outer + 1

def break_and_continue() -> int:
    total: int = 0
    i: int = 0
    for i in range(20):
        if i % 2 == 0:
            continue
        if i > 9:
            break
        total = total + i
    return total

def break_dead_code() -> int:
    i: int = 0
    while i < 100:
        if i == 3:
            break
        i = i + 1
    return i

print(break_while())
print(break_for())
nested_break()
print(break_and_continue())
print(break_dead_code())
