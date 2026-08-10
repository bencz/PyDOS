def create_counter(initial=0):
    count = initial

    def increment(step=1):
        nonlocal count
        count += step
        return count

    return increment


def plain():
    values = [number * 2 for number in range(3)]
    return values


counter = create_counter(10)
print(counter.__code__.co_name)
print(counter.__code__.co_freevars)
print(counter.__closure__ is not None)
print(counter.__closure__[0].cell_contents)
print(counter(5))
print(counter.__closure__[0].cell_contents)
print(plain.__code__.co_name)
print([value.co_name for value in plain.__code__.co_consts if hasattr(value, "co_name")])
