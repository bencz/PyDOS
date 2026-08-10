"""Closure capture across scope levels.

Three defects meet here.  A name used two scopes below its owner was never
added to the function in between, so the middle closure had no cell to hand
down.  The closure list is built with a discarded list_append whose result was
written to a negative temp slot, which aliased the first local and wiped the
enclosing parameter.  And a captured name was called by name instead of read
from its cell.
"""


def one_level(base):
    def inner(value):
        return value + base
    return inner


def one_level_direct(base):
    def inner(value):
        return value + base
    return inner(1)


def two_levels(base):
    def middle(step):
        def inner(value):
            return value + base + step
        return inner
    return middle


def two_levels_direct(base):
    def middle(step):
        def inner(value):
            return value + base + step
        return inner(1)
    return middle(2)


def grandparent_only(base):
    def middle(step):
        def inner(value):
            return value + base
        return inner(step)
    return middle(2)


def three_levels(a):
    def second(b):
        def third(c):
            def fourth(d):
                return a + b + c + d
            return fourth(4)
        return third(3)
    return second(2)


print(one_level(10)(1))
print(one_level_direct(10))
print(two_levels(10)(2)(1))
print(two_levels_direct(10))
print(grandparent_only(10))
print(three_levels(1))


def parameter_survives(param):
    def inner(value):
        return value + param
    result = inner(1)
    return result, param


print(parameter_survives(10))


def many_parameters(a, b, c):
    def inner(value):
        return value + a
    first = inner(1)
    return first, a, b, c


print(many_parameters(10, 20, 30))


def captured_callable(fn):
    def caller(value):
        return fn(value)
    return caller(5)


print(captured_callable(lambda v: v * 2))


def captured_callable_deep(fn):
    def middle(extra):
        def inner(value):
            return fn(value) + extra
        return inner(5)
    return middle(1)


print(captured_callable_deep(lambda v: v * 2))


def counter_with_nonlocal(start):
    total = start

    def bump(amount):
        nonlocal total
        total += amount
        return total
    bump(1)
    bump(2)
    return total, bump(3)


print(counter_with_nonlocal(0))


def nonlocal_two_levels(start):
    total = start

    def middle():
        def inner():
            nonlocal total
            total += 5
            return total
        return inner()
    first = middle()
    return first, total


print(nonlocal_two_levels(10))


def shared_cell(base):
    def reader():
        return base

    def writer(value):
        nonlocal base
        base = value
    writer(99)
    return reader()


print(shared_cell(1))


def capture_in_loop(values):
    built = []
    for value in values:
        def render(prefix):
            return prefix + str(value)
        built.append(render("<"))
    return built


print(capture_in_loop([1, 2, 3]))


def mixed_state(param):
    local = param * 2
    collected = []

    def record(tag):
        collected.append(tag + str(local) + str(param))
        return len(collected)
    record("a")
    record("b")
    return collected, local, param


print(mixed_state(3))


class Holder:
    def __init__(self, base: int) -> None:
        self.base = base

    def adder(self, step):
        def inner(value):
            return value + self.base + step
        return inner(1)

    def deep_adder(self, step):
        def middle(extra):
            def inner(value):
                return value + self.base + step + extra
            return inner(1)
        return middle(2)

    def keeps_arguments(self, first, second):
        def inner(value):
            return value + first
        computed = inner(1)
        return computed, first, second


holder = Holder(100)
print(holder.adder(10))
print(holder.deep_adder(10))
print(holder.keeps_arguments(5, 6))
