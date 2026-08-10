EVENTS = []


class Normalized:
    def __new__(klass, value):
        item = super().__new__(klass)
        item.value = value.strip().lower()
        return item


class Foreign:
    initialized = False

    def __new__(klass):
        return 42

    def __init__(self):
        Foreign.initialized = True


class Root:
    def process(this):
        return ("Root",)


class Left(Root):
    def process(this):
        return ("Left", *super().process())


class Right(Root):
    def process(this):
        return ("Right", *super().process())


class Diamond(Left, Right):
    def process(this):
        proxy = super()
        return ("Diamond", *proxy.process())


class Explicit(Left):
    def process(this):
        return super(Explicit, this).process()


print(Normalized("  PyDOS  ").value)
print(Foreign(), Foreign.initialized)
print(Diamond().process())
print(Explicit().process())
