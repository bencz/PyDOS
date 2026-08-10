events = []


for number in range(3):
    try:
        if number == 0:
            continue
        if number == 1:
            break
    finally:
        events.append("finally:" + str(number))


class Manager:
    def __init__(self, name):
        self.name = name

    def __enter__(self):
        events.append("enter:" + self.name)
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        events.append("exit:" + self.name)
        return False


for number in range(3):
    with Manager(str(number)):
        if number == 0:
            continue
        break


for number in range(2):
    try:
        def inner():
            for value in range(2):
                break
            return value

        if inner() == 0:
            continue
    finally:
        events.append("nested:" + str(number))


print(events)
