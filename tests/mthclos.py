"""Nested function definitions inside method bodies.

build_funcdef only emitted MAKE_FUNCTION when current_class_name was unset,
and that marker stayed live for the whole method body.  A def nested in a
method therefore never produced a callable object, while a lambda in the same
position did.  These cases cover both forms plus the decorator factories that
depend on them.
"""


class Adder:
    def __init__(self, base: int) -> None:
        self.base = base

    def make_plain(self):
        def inner(value):
            return value + 1
        return inner

    def make_capturing(self, extra):
        def inner(value):
            return value + extra
        return inner

    def make_self_capturing(self):
        def inner(value):
            return value + self.base
        return inner

    def make_lambda(self):
        return lambda value: value + self.base

    def make_nested_twice(self, outer):
        def middle(mid):
            def deepest(value):
                return value + outer + mid
            return deepest
        return middle

    @staticmethod
    def make_static(step):
        def inner(value):
            return value * step
        return inner

    @classmethod
    def make_class(cls, step):
        def inner(value):
            return value - step
        return inner


adder = Adder(10)
print(adder.make_plain()(5))
print(adder.make_capturing(100)(5))
print(adder.make_self_capturing()(5))
print(adder.make_lambda()(5))
print(adder.make_nested_twice(1)(2)(3))
print(Adder.make_static(3)(5))
print(Adder.make_class(4)(5))


class Registry:
    def __init__(self) -> None:
        self.actions = {}

    def command(self, name):
        def register(fn):
            self.actions[name] = fn
            return fn
        return register

    def run(self, name, value):
        handler = self.actions[name]
        return handler(value)


registry = Registry()


@registry.command("double")
def double(value):
    return value * 2


@registry.command("negate")
def negate(value):
    return -value


print(len(registry.actions))
print(registry.run("double", 21))
print(registry.run("negate", 7))
print(double(3), negate(3))


class Bindings:
    table = {}

    @classmethod
    def on_key(cls, name):
        def register(fn):
            cls.table[name] = fn
            return fn
        return register

    @staticmethod
    def wrap(prefix):
        def decorate(fn):
            def wrapper(value):
                return prefix + fn(value)
            return wrapper
        return decorate


class Editor:
    @Bindings.on_key("ctrl+s")
    def save(self):
        return "saved"

    @Bindings.on_key("ctrl+q")
    def quit(self):
        return "quit"


print(len(Bindings.table))
print(Editor().save(), Editor().quit())


@Bindings.wrap(">> ")
def describe(value):
    return str(value)


print(describe(42))


class Counter:
    def __init__(self) -> None:
        self.total = 0

    def make_accumulator(self):
        def add(amount):
            self.total += amount
            return self.total
        return add

    def make_stateful(self, start):
        state = [start]

        def step():
            state[0] += 1
            return state[0]
        return step


counter = Counter()
accumulate = counter.make_accumulator()
print(accumulate(5), accumulate(10), counter.total)

stepper = counter.make_stateful(100)
print(stepper(), stepper(), stepper())


class Pipeline:
    def __init__(self, stages) -> None:
        self.stages = stages

    def build(self):
        def run(value):
            result = value
            for stage in self.stages:
                result = stage(result)
            return result
        return run

    def make_all(self):
        built = []
        for factor in [2, 3, 4]:
            built.append(self.scaler(factor))
        return built

    def scaler(self, factor):
        def scale(value):
            return value * factor
        return scale


pipeline = Pipeline([lambda v: v + 1, lambda v: v * 10])
print(pipeline.build()(4))

scalers = pipeline.make_all()
outputs = []
for scaler in scalers:
    outputs.append(scaler(5))
print(outputs)


class Generators:
    def __init__(self, values) -> None:
        self.values = values

    def filtered(self, limit):
        def keep(value):
            return value < limit
        for value in self.values:
            if keep(value):
                yield value


print(list(Generators([1, 5, 2, 8, 3]).filtered(4)))
