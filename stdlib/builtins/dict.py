# dict.py - Python dictionary class for PyDOS

from _internal import internal_implementation

class dict:
    @internal_implementation("pydos_dict_get_op_")
    def __getitem__(self, key):
        pass

    @internal_implementation("pydos_dict_set_")
    def __setitem__(self, key, value) -> None:
        pass

    @internal_implementation("pydos_obj_contains_")
    def __contains__(self, key) -> bool:
        pass

    def get(self, key, default=None):
        if key in self:
            return self[key]
        return default

    def keys(self) -> list:
        result: list = []
        for key in self:
            result.append(key)
        return result

    def values(self) -> list:
        result: list = []
        for key in self:
            result.append(self[key])
        return result

    def items(self) -> list:
        result: list = []
        for key in self:
            result.append((key, self[key]))
        return result

    def pop(self, key):
        if key not in self:
            raise KeyError("dict.pop(key): key not found")
        value: object = self[key]
        del self[key]
        return value

    def update(self, other) -> None:
        for key in other:
            self[key] = other[key]

    @internal_implementation("pydos_dict_clear_m_")
    def clear(self) -> None:
        pass

    def setdefault(self, key, default=None):
        if key not in self:
            self[key] = default
        return self[key]

    def copy(self) -> dict:
        result: dict = {}
        for key in self:
            result[key] = self[key]
        return result

    def popitem(self) -> tuple:
        last_key: object = None
        found: bool = False
        for key in self:
            last_key = key
            found = True
        if not found:
            raise KeyError("popitem(): dictionary is empty")
        value: object = self[last_key]
        del self[last_key]
        return (last_key, value)
