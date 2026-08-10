"""Same inner name defined in many scopes.

Nested definitions took the plain Python name as their assembly symbol, so a
second "inner" or "wrapper" anywhere in the program produced a duplicate
label.  Every scope below deliberately reuses the same names.
"""


def first():
    def inner():
        return 1
    return inner


def second():
    def inner():
        return 2
    return inner


def third():
    def inner():
        return 3

    def helper():
        return 30
    return inner() + helper()


print(first()(), second()(), third())


class Alpha:
    def build(self):
        def inner():
            return "alpha.build"
        return inner

    def other(self):
        def inner():
            return "alpha.other"
        return inner


class Beta:
    def build(self):
        def inner():
            return "beta.build"
        return inner

    def other(self):
        def inner():
            def inner_inner():
                return "beta.other.deep"
            return inner_inner()
        return inner


print(Alpha().build()(), Alpha().other()())
print(Beta().build()(), Beta().other()())


def shadowing(value):
    def wrapper(x):
        return x + value

    def apply(x):
        def wrapper(y):
            return y * value
        return wrapper(x)
    return wrapper(1), apply(2)


print(shadowing(10))


def recursive_nested(n):
    def inner(k):
        if k <= 1:
            return 1
        return k * inner(k - 1)
    return inner(n)


def recursive_nested_other(n):
    def inner(k):
        if k <= 0:
            return 0
        return k + inner(k - 1)
    return inner(n)


print(recursive_nested(5), recursive_nested_other(5))


class Repeated:
    def one(self):
        def wrapper():
            return "one"
        return wrapper()

    def two(self):
        def wrapper():
            return "two"
        return wrapper()

    @staticmethod
    def three():
        def wrapper():
            return "three"
        return wrapper()

    @classmethod
    def four(cls):
        def wrapper():
            return "four"
        return wrapper()


repeated = Repeated()
print(repeated.one(), repeated.two(), Repeated.three(), Repeated.four())


def factories():
    built = []

    def make(tag):
        def render():
            return "<" + tag + ">"
        return render
    for name in ["a", "b", "c"]:
        built.append(make(name))
    return built


rendered = []
for render in factories():
    rendered.append(render())
print(rendered)


def conditional(flag):
    if flag:
        def choice():
            return "yes"
    else:
        def choice():
            return "no"
    return choice()


print(conditional(True), conditional(False))
