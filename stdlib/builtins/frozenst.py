# frozenst.py - Python frozenset class for PyDOS (8.3 filename)

from _internal import type_operators

@type_operators(contains="pydos_obj_contains_")
class frozenset:
    def union(self, other) -> frozenset:
        result: list = []
        for item in self:
            result.append(item)
        for item in other:
            result.append(item)
        return _pydos_frozenset_from_list(result)

    def intersection(self, other) -> frozenset:
        result: list = []
        for item in self:
            if item in other:
                result.append(item)
        return _pydos_frozenset_from_list(result)

    def difference(self, other) -> frozenset:
        result: list = []
        for item in self:
            if item not in other:
                result.append(item)
        return _pydos_frozenset_from_list(result)

    def symmetric_difference(self, other) -> frozenset:
        result: list = []
        for item in self:
            if item not in other:
                result.append(item)
        for item in other:
            if item not in self:
                result.append(item)
        return _pydos_frozenset_from_list(result)

    def copy(self) -> object:
        return self

    def issubset(self, other) -> bool:
        for item in self:
            if item not in other:
                return False
        return True

    def issuperset(self, other) -> bool:
        for item in other:
            if item not in self:
                return False
        return True

    def isdisjoint(self, other) -> bool:
        for item in self:
            if item in other:
                return False
        return True
