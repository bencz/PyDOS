class Flexible:
    def __init__(self, name):
        self.name = name
        self.temporary = "remove"

    def __getattribute__(self, name):
        if name == "upper_name":
            return object.__getattribute__(self, "name").upper()
        return object.__getattribute__(self, name)

    def __getattr__(self, name):
        return f"missing:{name}"

    def __setattr__(self, name, value):
        if name == "name" and (not isinstance(value, str) or not value.strip()):
            raise ValueError("invalid name")
        object.__setattr__(self, name, value)

    def __delattr__(self, name):
        if name == "name":
            raise AttributeError("protected")
        object.__delattr__(self, name)


item = Flexible("Ada")
print(item.upper_name, item.unknown)
item.name = "Grace"
print(item.name)
del item.temporary
print(item.temporary)
try:
    del item.name
except AttributeError as error:
    print(type(error).__name__, str(error))
try:
    item.name = ""
except ValueError as error:
    print(type(error).__name__, str(error))
