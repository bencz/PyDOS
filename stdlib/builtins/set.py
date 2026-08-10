# set.py - Python set class for PyDOS

from _internal import internal_implementation

class set:
    @internal_implementation("pydos_obj_contains_")
    def __contains__(self, item) -> bool:
        pass

    @internal_implementation("pydos_set_add_m_")
    def add(self, item) -> None:
        pass

    @internal_implementation("pydos_set_remove_m_")
    def remove(self, item) -> None:
        pass

    @internal_implementation("pydos_set_discard_m_")
    def discard(self, item) -> None:
        pass

    @internal_implementation("pydos_set_clear_m_")
    def clear(self) -> None:
        pass

    @internal_implementation("pydos_set_pop_")
    def pop(self):
        pass

    def union(self, other) -> set:
        result = _pydos_set_empty()
        for item in self:
            result.add(item)
        for item in other:
            result.add(item)
        return result

    def intersection(self, other) -> set:
        result = _pydos_set_empty()
        for item in self:
            if item in other:
                result.add(item)
        return result

    def difference(self, other) -> set:
        result = _pydos_set_empty()
        for item in self:
            if item not in other:
                result.add(item)
        return result

    def symmetric_difference(self, other) -> set:
        result = _pydos_set_empty()
        for item in self:
            if item not in other:
                result.add(item)
        for item in other:
            if item not in self:
                result.add(item)
        return result

    def copy(self) -> set:
        result = _pydos_set_empty()
        for item in self:
            result.add(item)
        return result

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

    def update(self, other) -> None:
        for item in other:
            self.add(item)

    def intersection_update(self, other) -> None:
        removed: list = []
        for item in self:
            if item not in other:
                removed.append(item)
        for item in removed:
            self.discard(item)

    def difference_update(self, other) -> None:
        removed: list = []
        for item in self:
            if item in other:
                removed.append(item)
        for item in removed:
            self.discard(item)

    def symmetric_difference_update(self, other) -> None:
        removed: list = []
        added: list = []
        for item in self:
            if item in other:
                removed.append(item)
        for item in other:
            if item not in self:
                added.append(item)
        for item in removed:
            self.discard(item)
        for item in added:
            self.add(item)
