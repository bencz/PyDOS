"""Call forms: what is invoked, and in which order the callee is evaluated.

Two defects are covered here.  Arguments used to be pushed before the callee
expression was built, so "obj.make()(5)" ran the inner call while the outer
call's arguments already sat on the arg stack.  And a captured name was called
by name, which looks for a module function instead of reading the cell, so a
closure could not call the function it had captured.
"""


def increment(value):
    return value + 1


def make_lambda():
    return lambda value: value + 1


def make_nested():
    def inner(value):
        return value + 1
    return inner


def make_named():
    return increment


class Factory:
    def __init__(self, step=1) -> None:
        self.step = step

    def make_lambda(self):
        return lambda value: value + self.step

    def make_nested(self):
        def inner(value):
            return value + self.step
        return inner

    def make_named(self):
        return increment

    @staticmethod
    def make_static():
        def inner(value):
            return value + 100
        return inner

    @classmethod
    def make_class(cls):
        def inner(value):
            return value + 1000
        return inner


print(make_lambda()(5), make_nested()(5), make_named()(5))

factory = Factory(2)
print(factory.make_lambda()(5))
print(factory.make_nested()(5))
print(factory.make_named()(5))
print(Factory.make_static()(5))
print(Factory.make_class()(5))
print(Factory(10).make_nested()(5))

held = factory.make_nested()
print(held(5))

print(Factory(3).make_lambda()(1), Factory(4).make_lambda()(1))


def call_captured(fn):
    def caller(value):
        return fn(value)
    return caller


def call_captured_twice(fn):
    def middle(value):
        def deepest(inner_value):
            return fn(inner_value) + fn(value)
        return deepest
    return middle


print(call_captured(increment)(5))
print(call_captured(lambda v: v * 3)(5))
print(call_captured_twice(increment)(1)(2))


def wrap(prefix):
    def decorate(fn):
        def wrapper(value):
            return prefix + fn(value)
        return wrapper
    return decorate


@wrap(">> ")
def describe(value):
    return str(value)


print(describe(42))


class Registry:
    def __init__(self) -> None:
        self.handlers = {}

    def register(self, name):
        def bind(fn):
            self.handlers[name] = fn
            return fn
        return bind


registry = Registry()


@registry.register("inc")
def registered_increment(value):
    return value + 1


print(len(registry.handlers), registry.handlers["inc"](41))


def choose(flag):
    if flag:
        return increment
    return lambda value: value - 1


print(choose(True)(10), choose(False)(10))

table = {"inc": increment, "dec": lambda v: v - 1}
print(table["inc"](5), table["dec"](5))

callables = [increment, make_lambda(), make_nested()]
results = []
for item in callables:
    results.append(item(1))
print(results)


def run_all(functions, value):
    total = 0
    for fn in functions:
        total += fn(value)
    return total


print(run_all(callables, 10))


class Counter:
    def __init__(self) -> None:
        self.value = 0

    def bump(self, amount):
        self.value += amount
        return self

    def total(self):
        return self.value


print(Counter().bump(2).bump(3).total())

counter = Counter()
print(counter.bump(1).bump(1).total())


def outer_args(a, b):
    def inner(c):
        return a * 100 + b * 10 + c
    return inner


print(outer_args(1, 2)(3))
print(outer_args(1, 2)(3) + outer_args(4, 5)(6))


class Applier:
    def apply(self, fn, value):
        return fn(value)

    def apply_twice(self, fn, value):
        return fn(fn(value))


applier = Applier()
print(applier.apply(increment, 5))
print(applier.apply_twice(increment, 5))
print(applier.apply(make_nested(), 5))
print(applier.apply(factory.make_nested(), 5))
