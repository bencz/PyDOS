"""High-level built-in descriptors for PyDOS.

Stored as descript.py for DOS 8.3; its logical module name remains
builtins.descriptors.

The attribute lookup and method-binding mechanisms are runtime protocols;
the user-visible policy stays in Python so it remains inspectable and can
evolve without growing the C runtime.
"""


class staticmethod:
    def __init__(self, func):
        self.__func__ = func
        self.__wrapped__ = func
        self.__isabstractmethod__ = getattr(
            func, "__isabstractmethod__", False
        )

    def __get__(self, obj, owner=None):
        return self.__func__


class classmethod:
    def __init__(self, func):
        self.__func__ = func
        self.__wrapped__ = func
        self.__isabstractmethod__ = getattr(
            func, "__isabstractmethod__", False
        )

    def __get__(self, obj, owner=None):
        return _pydos_bind_method(self.__func__, owner)


class property:
    def __init__(self, fget=None, fset=None, fdel=None, doc=None):
        self.fget = fget
        self.fset = fset
        self.fdel = fdel
        self.__doc__ = doc
        self.__isabstractmethod__ = (
            getattr(fget, "__isabstractmethod__", False)
            or getattr(fset, "__isabstractmethod__", False)
            or getattr(fdel, "__isabstractmethod__", False)
        )

    def __get__(self, obj, owner=None):
        if obj is None:
            return self
        if self.fget is None:
            raise AttributeError("property has no getter")
        getter = self.fget
        return getter(obj)

    def __set__(self, obj, value):
        if self.fset is None:
            raise AttributeError("property has no setter")
        setter = self.fset
        setter(obj, value)

    def __delete__(self, obj):
        if self.fdel is None:
            raise AttributeError("property has no deleter")
        deleter = self.fdel
        deleter(obj)

    def getter(self, fget):
        return property(fget, self.fset, self.fdel, self.__doc__)

    def setter(self, fset):
        return property(self.fget, fset, self.fdel, self.__doc__)

    def deleter(self, fdel):
        return property(self.fget, self.fset, fdel, self.__doc__)
