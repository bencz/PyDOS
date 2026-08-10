# Remaining simple Python 3.12 builtin forms.

print(sum([1, 2, 3], 10))
print(list(enumerate(["a", "b"], 5)))
print(list(filter(None, [0, 1, False, 2, "", "x"])))

iterator = iter([7])
print(next(iterator))
print(next(iterator, 99))
try:
    next(iterator)
except StopIteration:
    print("stopped")

class Box:
    pass

instance: Box = Box()
setattr(instance, "value", 42)
print(getattr(instance, "value"))
print(hasattr(instance, "value"))
delattr(instance, "value")
print(hasattr(instance, "value"))
print(getattr(instance, "value", 99))
try:
    getattr(instance, "value")
except AttributeError:
    print("missing attr")

def identity(value):
    return value

print(callable(identity))
print(callable(Box))
print(callable(42))
print(getattr(range(3, 9, 2), "step"))

class Base:
    pass

class Derived(Base):
    pass

derived: Derived = Derived()
print(Derived)
print(Derived.__name__)
print(Derived.__bases__[0] is Base)
derived_type = type(derived)
print(derived_type is Derived)
print(derived.__class__ is Derived)
print(isinstance(derived, Derived))
print(isinstance(derived, Base))
print(issubclass(Derived, Base))

setattr(Base, "label", "base")
print(Base.label)
print(Derived.label)
print(derived.label)
setattr(derived, "own", 7)
print(vars(derived)["own"])
print(vars(Base)["label"])

class Value:
    def __init__(self, number: int):
        self.number = number

factory = Value
dynamic_value: Value = factory(23)
print(dynamic_value.number)
dynamic_type = type(dynamic_value)
print(dynamic_type is Value)

original_tuple = (1, 2)
print(tuple())
print(tuple([1, 2, 3]))
print(tuple(original_tuple) is original_tuple)

print(type(42) is int)
print(type("x") is str)
print(type(int) is type)
print(int.__name__)
print(int.__module__)
integer_type = int
boolean_type = bool
print(isinstance(42, integer_type))
print(issubclass(boolean_type, integer_type))
root_object = object()
print(type(root_object) is object)
print(isinstance(root_object, object))
print(isinstance(42, object))
print(issubclass(int, object))
print(Base.__bases__[0] is object)
print(issubclass(Base, object))
print(object.__bases__ == ())
print(int.__bases__[0] is object)
print(bool.__bases__[0] is int)
integer_factory = int
print(integer_factory("12"))
object_factory = object
second_root = object_factory()
print(type(second_root) is object)

class Reader:
    def __init__(self, value: int):
        self.value = value

    def read(self) -> int:
        return self.value

reader: Reader = Reader(31)
bound_read = reader.read
print(bound_read())
getattr_read = getattr(reader, "read")
print(getattr_read())
unbound_read = getattr(Reader, "read")
print(unbound_read(reader))

class BrokenInit:
    def __init__(self):
        return 1

broken_factory = BrokenInit
try:
    broken_factory()
except TypeError:
    print("bad init")
