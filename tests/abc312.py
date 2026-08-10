from abc import (
    ABC, ABCMeta, abstractmethod, get_cache_token,
    update_abstractmethods
)


print(callable(ABCMeta))
print(type(ABC) is ABCMeta)
print(hasattr(ABC, "register"))


class AbstractThing(ABC):
    @abstractmethod
    def value(self):
        pass


print(AbstractThing.__abstractmethods__)
print(getattr(AbstractThing.value, "__isabstractmethod__", False))

try:
    AbstractThing()
    print("not blocked")
except TypeError:
    print("blocked")


class ConcreteThing(AbstractThing):
    def value(self):
        return 42


print(ConcreteThing.__abstractmethods__)
print(ConcreteThing().value())


class LateAbstract(ABC):
    def value(self):
        return 7


LateAbstract.value = abstractmethod(LateAbstract.value)
update_abstractmethods(LateAbstract)
try:
    LateAbstract()
    print("late not blocked")
except TypeError:
    print("late blocked")


class Explicit(metaclass=ABCMeta):
    @abstractmethod
    def execute(self):
        pass


print(type(Explicit) is ABCMeta)
try:
    Explicit()
    print("explicit not blocked")
except TypeError:
    print("explicit blocked")


class Protocol(ABC):
    @abstractmethod
    def run(self):
        pass


class Foreign:
    def run(self):
        return "foreign"


print(issubclass(Foreign, Protocol))
token_before = get_cache_token()
print(Protocol.register(Foreign) is Foreign)
print(get_cache_token() != token_before)
print(issubclass(Foreign, Protocol))
print(isinstance(Foreign(), Protocol))


class ForeignChild(Foreign):
    pass


print(issubclass(ForeignChild, Protocol))


@Protocol.register
class DecoratedForeign:
    def run(self):
        return "decorated"


print(issubclass(DecoratedForeign, Protocol))


class NarrowProtocol(Protocol):
    pass


class NarrowForeign:
    pass


NarrowProtocol.register(NarrowForeign)
print(issubclass(NarrowForeign, NarrowProtocol))
print(issubclass(NarrowForeign, Protocol))


class RootOnlyForeign:
    pass


Protocol.register(RootOnlyForeign)
print(issubclass(RootOnlyForeign, Protocol))
print(issubclass(RootOnlyForeign, NarrowProtocol))

try:
    Protocol.register(ABC)
    print("cycle not rejected")
except RuntimeError:
    print("cycle rejected")


class DescriptorAbstract(ABC):
    @property
    @abstractmethod
    def item(self):
        pass

    @classmethod
    @abstractmethod
    def create(cls):
        pass

    @staticmethod
    @abstractmethod
    def validate(value):
        pass


print(len(DescriptorAbstract.__abstractmethods__))
print("item" in DescriptorAbstract.__abstractmethods__)
print("create" in DescriptorAbstract.__abstractmethods__)
print("validate" in DescriptorAbstract.__abstractmethods__)
try:
    DescriptorAbstract()
    print("descriptor not blocked")
except TypeError:
    print("descriptor blocked")


class DescriptorConcrete(DescriptorAbstract):
    @property
    def item(self):
        return 11

    @classmethod
    def create(cls):
        return cls()

    @staticmethod
    def validate(value):
        return value > 0


print(DescriptorConcrete.__abstractmethods__)
descriptor_value = DescriptorConcrete.create()
print(descriptor_value.item)
print(DescriptorConcrete.validate(3))


class Structural(ABC):
    @classmethod
    def __subclasshook__(cls, candidate):
        return hasattr(candidate, "marker")


class StructurallyMarked:
    def marker(self):
        return True


class StructurallyUnmarked:
    pass


print(issubclass(StructurallyMarked, Structural))
print(issubclass(StructurallyUnmarked, Structural))
print(isinstance(StructurallyMarked(), Structural))

token_before = get_cache_token()
Protocol._abc_registry_clear()
print(get_cache_token() != token_before)
print(issubclass(Foreign, Protocol))
