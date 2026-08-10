"""Runtime-checkable collection protocols used by Python 3.12 code."""

from abc import ABC


class Callable(ABC):
    @classmethod
    def __subclasshook__(cls, candidate):
        if hasattr(candidate, "__call__"):
            return True
        return None


class Iterator(ABC):
    @classmethod
    def __subclasshook__(cls, candidate):
        if hasattr(candidate, "__iter__") and hasattr(candidate, "__next__"):
            return True
        return None


class Generator(Iterator):
    @classmethod
    def __subclasshook__(cls, candidate):
        if hasattr(candidate, "send") and hasattr(candidate, "throw"):
            return True
        return Iterator.__subclasshook__(candidate)


class Buffer(ABC):
    @classmethod
    def __subclasshook__(cls, candidate):
        if hasattr(candidate, "__buffer__"):
            return True
        return None
