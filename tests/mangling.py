"""Private name mangling and inherited descriptors.

An identifier written __name inside a class body binds _ClassName__name, so a
base class and a subclass each keep their own.  Nothing was mangled before, so
a base method calling self.__m() reached the subclass override and _A__x did
not exist.  The inherited classmethod belongs here too: the vtable lookup also
answers for inherited methods and used to return the raw function ahead of the
descriptor that binds cls.
"""


class Vault:
    __class_secret = "base-class secret"
    _non_public_attribute = "convention only"
    public_attribute = "public API"

    def __init__(self, secret: str) -> None:
        self.__secret = secret

    def __private_method(self) -> str:
        return "base:" + self.__secret

    def reveal_base_secret(self) -> str:
        return self.__private_method()

    def secret_length(self) -> int:
        return len(self.__secret)

    @classmethod
    def class_secret(cls) -> str:
        return cls.__class_secret

    @staticmethod
    def base_mangled_name(name: str) -> str:
        return "_Vault__" + name


class SpecialVault(Vault):
    __class_secret = "subclass secret"

    def __init__(self, base_secret: str, special_secret: str) -> None:
        super().__init__(base_secret)
        self.__secret = special_secret

    def __private_method(self) -> str:
        return "subclass:" + self.__secret

    def reveal_special_secret(self) -> str:
        return self.__private_method()

    @classmethod
    def subclass_secret(cls) -> str:
        return cls.__class_secret


vault = SpecialVault("ALPHA", "BETA")
print(vault.reveal_base_secret())
print(vault.reveal_special_secret())
print(vault.secret_length())
print(vault.public_attribute, vault._non_public_attribute)

print(Vault.class_secret())
print(SpecialVault.class_secret())
print(SpecialVault.subclass_secret())
print(Vault.base_mangled_name("secret"))
print(SpecialVault.base_mangled_name("secret"))

print(vault._Vault__secret, vault._SpecialVault__secret)
print(vault._Vault__private_method())
print(vault._SpecialVault__private_method())
print(Vault._Vault__class_secret, SpecialVault._SpecialVault__class_secret)
print(SpecialVault._Vault__class_secret)

plain = Vault("SOLO")
print(plain.reveal_base_secret(), plain._Vault__secret)


class Sentinel:
    __token = "hidden"
    __dunder__ = "not mangled"
    _single = "not mangled either"

    def __init__(self) -> None:
        self.__value = 1
        self._protected = 2
        self.public = 3

    def bump(self) -> int:
        self.__value += 1
        return self.__value

    def snapshot(self) -> tuple:
        return (self.__value, self._protected, self.public, self.__token)

    def helper(self) -> str:
        return self.__format()

    def __format(self) -> str:
        return "<" + str(self.__value) + ">"


sentinel = Sentinel()
print(sentinel.bump(), sentinel.bump())
print(sentinel.snapshot())
print(sentinel.helper())
print(sentinel._Sentinel__value, sentinel._Sentinel__token)
print(Sentinel.__dunder__, Sentinel._single)
print(sentinel._Sentinel__format())


class Base:
    @classmethod
    def make(cls, tag: str = "t"):
        return cls.__name__ + ":" + tag

    @staticmethod
    def stat(value: int = 7) -> int:
        return value * 2

    @property
    def kind(self) -> str:
        return "base"


class Derived(Base):
    @property
    def kind(self) -> str:
        return "derived"


class Untouched(Base):
    pass


print(Base.make(), Derived.make(), Untouched.make())
print(Base.make("x"), Untouched.make("y"))
print(Base.stat(), Untouched.stat(), Untouched.stat(3))
print(Base().kind, Derived().kind, Untouched().kind)


class Counter:
    __count = 0

    def __init__(self) -> None:
        Counter.__count += 1

    @classmethod
    def total(cls) -> int:
        return cls.__count


Counter()
Counter()
Counter()
print(Counter.total(), Counter._Counter__count)
