def top(fn):
    print("apply top")
    return fn


def bottom(fn):
    print("apply bottom")
    return fn


def choose_top():
    print("eval top")
    return top


def choose_bottom():
    print("eval bottom")
    return bottom


@choose_top()
@choose_bottom()
def decorated():
    return "body"


print(decorated())


def replacement():
    return "replacement"


def replace(fn):
    return replacement


@replace
def replaced():
    return "original"


print(replaced())


def passthrough(fn):
    return fn


@passthrough
def add(a, b):
    return a + b


print(add(2, 3))


def class_top(cls):
    print("apply class top")
    return cls


def class_bottom(cls):
    print("apply class bottom")
    return cls


@class_top
@class_bottom
class Ordered:
    pass


print(Ordered.__name__)


class Alternate:
    def value(self):
        return "alternate"


def replace_class(cls):
    return Alternate


@replace_class
class Original:
    def value(self):
        return "original"


print(Original().value())


def alternate_value(self):
    return "decorated method"


def use_alternate(fn):
    return alternate_value


class MethodDemo:
    @use_alternate
    def value(self):
        return "original method"


method_demo = MethodDemo()
print(method_demo.value())
print(getattr(method_demo, "value")())
print(MethodDemo.value(method_demo))
