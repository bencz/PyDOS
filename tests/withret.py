events = []


class Manager:
    def __init__(self, name, suppress=False, fail_enter=False):
        self.name = name
        self.suppress = suppress
        self.fail_enter = fail_enter

    def __enter__(self):
        events.append(self.name + ":enter")
        if self.fail_enter:
            raise ValueError(self.name)
        return self.name

    def __exit__(self, exc_type, exc_value, traceback):
        if exc_value is None:
            events.append(self.name + ":normal")
        else:
            events.append(self.name + ":error")
        print(exc_type is exc_value)
        print(traceback is None)
        return self.suppress


def return_inside_with():
    with Manager("return") as value:
        events.append(value)
        return 7


print(return_inside_with())

with Manager("suppress", suppress=True):
    raise ValueError("hidden")
events.append("continued")

try:
    with Manager("outer"), Manager("inner", fail_enter=True):
        events.append("unreachable")
except ValueError:
    events.append("caught")

print(events)
