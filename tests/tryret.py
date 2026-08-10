events = []


def return_from_try():
    try:
        events.append("try")
        return 10
    finally:
        events.append("finally")


def return_from_except():
    try:
        raise ValueError("test")
    except ValueError:
        events.append("except")
        return 20
    finally:
        events.append("except-finally")


def nested_finally():
    try:
        try:
            events.append("inner")
            return 30
        finally:
            events.append("inner-finally")
    finally:
        events.append("outer-finally")


def try_else(value):
    try:
        if value:
            events.append("try-ok")
        else:
            raise ValueError("no")
    except ValueError:
        events.append("handled")
    else:
        events.append("else")
    return 40


print(return_from_try())
print(return_from_except())
print(nested_finally())
print(try_else(True))
print(try_else(False))
print(events)
