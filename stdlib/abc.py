"""Abstract base classes for PyDOS.

ABC policy is implemented here.  The runtime supplies only the class,
metaclass, descriptor and nominal/virtual subtype protocols needed to keep
lookup efficient on 8086 and 386 targets.
"""

_abc_token_box = [0]

def abstractmethod(funcobj):
    setattr(funcobj, "__isabstractmethod__", True)
    return funcobj


def update_abstractmethods(cls):
    """Recalculate ``cls.__abstractmethods__`` after class mutation."""
    names = []
    namespace = vars(cls)

    for base in cls.__bases__:
        inherited = getattr(base, "__abstractmethods__", ())
        for name in inherited:
            if name in namespace:
                value = namespace[name]
            else:
                value = getattr(cls, name, None)
            if getattr(value, "__isabstractmethod__", False):
                if name not in names:
                    names.append(name)

    for name in namespace:
        value = namespace[name]
        if getattr(value, "__isabstractmethod__", False):
            if name not in names:
                names.append(name)

    setattr(cls, "__abstractmethods__", frozenset(names))
    return cls


class ABCMeta:
    """Metaclass used by :class:`ABC` and explicit ``metaclass=`` classes."""

    def __pydos_metaclass_init__(cls):
        # Each ABC owns its registry.  It must not be inherited, because a
        # registration on a base ABC does not make it a registration on a
        # more-specific derived ABC.
        setattr(cls, "__abc_registry__", [])
        update_abstractmethods(cls)

    def register(cls, subclass):
        if not isinstance(subclass, type):
            raise TypeError("Can only register classes")
        if issubclass(cls, subclass):
            raise RuntimeError("Refusing to create an inheritance cycle")
        if issubclass(subclass, cls):
            return subclass

        registry = vars(cls)["__abc_registry__"]
        if subclass not in registry:
            registry.append(subclass)
            _abc_token_box[0] += 1

        # A virtual subclass of a derived ABC is also a virtual subclass of
        # each ABC base, while the reverse is intentionally not true.
        for base in cls.__bases__:
            if "__abc_registry__" in vars(base):
                base.register(subclass)
        return subclass

    def __subclasscheck__(cls, subclass):
        return issubclass(subclass, cls)

    def __instancecheck__(cls, instance):
        return isinstance(instance, cls)

    def _abc_registry_clear(cls):
        registry = vars(cls)["__abc_registry__"]
        registry.clear()

    def _abc_caches_clear(cls):
        # PyDOS deliberately has no ABC positive/negative caches yet.
        pass


class ABC(metaclass=ABCMeta):
    pass


def get_cache_token():
    return _abc_token_box[0]


class abstractclassmethod(classmethod):
    def __init__(self, callable_obj):
        classmethod.__init__(self, abstractmethod(callable_obj))


class abstractstaticmethod(staticmethod):
    def __init__(self, callable_obj):
        staticmethod.__init__(self, abstractmethod(callable_obj))


class abstractproperty(property):
    def __init__(self, fget=None, fset=None, fdel=None, doc=None):
        if fget is not None:
            abstractmethod(fget)
        property.__init__(self, fget, fset, fdel, doc)
