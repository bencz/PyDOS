# pyfncs.py - Python-backed builtin implementations
#
# These functions are compiled by --build-stdlib to PIR,
# serialized into stdlib.idx v3, and merged into user code
# at compile time when called.
#
# Rules:
#   - No imports (no @internal_implementation, no _internal)
#   - No decorators
#   - Only use language features the compiler supports
#   - Function names must match py_name in funcs.py stubs

def any(iterable) -> bool:
    for item in iterable:
        if item:
            return True
    return False

def all(iterable) -> bool:
    for item in iterable:
        if not item:
            return False
    return True

def sum(iterable) -> int:
    result: int = 0
    for item in iterable:
        result = result + item
    return result

def min(a, b) -> object:
    if a < b:
        return a
    return b

def max(a, b) -> object:
    if a > b:
        return a
    return b

def enumerate(iterable) -> object:
    i: int = 0
    for item in iterable:
        yield (i, item)
        i = i + 1

def zip(a, b) -> object:
    i: int = 0
    la: int = len(a)
    lb: int = len(b)
    n: int = la
    if lb < la:
        n = lb
    while i < n:
        yield (a[i], b[i])
        i = i + 1

def map(func, iterable) -> object:
    for item in iterable:
        yield func(item)

def filter(func, iterable) -> object:
    for item in iterable:
        if func(item):
            yield item

def sorted(iterable) -> list:
    result: list = list(iterable)
    result.sort()
    return result

def reversed(seq) -> object:
    i: int = len(seq) - 1
    while i >= 0:
        yield seq[i]
        i = i - 1

# --- Method implementations ---
# Convention: TYPE_METHOD(self, ...) maps to TYPE.METHOD()

def list_count(self, value) -> int:
    n: int = 0
    for x in self:
        if x == value:
            n = n + 1
    return n

def list_extend(self, iterable) -> None:
    for x in iterable:
        self.append(x)

def tuple_count(self, value) -> int:
    n: int = 0
    for x in self:
        if x == value:
            n = n + 1
    return n

def tuple_index(self, value) -> int:
    i: int = 0
    for x in self:
        if x == value:
            return i
        i = i + 1
    raise ValueError("tuple.index(x): x not in tuple")

def dict_update(self, other) -> None:
    for k in other:
        self[k] = other[k]

def dict_copy(self) -> dict:
    result: dict = {}
    for k in self:
        result[k] = self[k]
    return result

def dict_popitem(self) -> tuple:
    last_key: object = None
    found: bool = False
    for k in self:
        last_key = k
        found = True
    if not found:
        raise KeyError("popitem(): dictionary is empty")
    val: object = self[last_key]
    del self[last_key]
    return (last_key, val)

def set_update(self, other) -> None:
    for x in other:
        self.add(x)

def set_copy(self) -> set:
    result: set = {0}
    result.clear()
    for x in self:
        result.add(x)
    return result

def set_issubset(self, other) -> bool:
    for x in self:
        if x not in other:
            return False
    return True

def set_issuperset(self, other) -> bool:
    for x in other:
        if x not in self:
            return False
    return True

def set_isdisjoint(self, other) -> bool:
    for x in self:
        if x in other:
            return False
    return True

def set_union(self, other) -> set:
    result: set = set_copy(self)
    for x in other:
        result.add(x)
    return result

def set_intersection(self, other) -> set:
    result: set = {0}
    result.clear()
    for x in self:
        if x in other:
            result.add(x)
    return result

def set_difference(self, other) -> set:
    result: set = {0}
    result.clear()
    for x in self:
        if x not in other:
            result.add(x)
    return result

def set_symmetric_difference(self, other) -> set:
    result: set = {0}
    result.clear()
    for x in self:
        if x not in other:
            result.add(x)
    for x in other:
        if x not in self:
            result.add(x)
    return result

def dict_keys(self) -> list:
    result: list = []
    for k in self:
        result.append(k)
    return result

def dict_values(self) -> list:
    result: list = []
    for k in self:
        result.append(self[k])
    return result

def dict_items(self) -> list:
    result: list = []
    for k in self:
        result.append((k, self[k]))
    return result

def dict_pop(self, key) -> object:
    if key not in self:
        raise KeyError("dict.pop(key): key not found")
    val: object = self[key]
    del self[key]
    return val

def dict_setdefault(self, key, default) -> object:
    if key not in self:
        self[key] = default
    return self[key]

def list_index(self, item) -> int:
    i: int = 0
    for x in self:
        if x == item:
            return i
        i = i + 1
    raise ValueError("list.index(x): x not in list")

def list_remove(self, item) -> None:
    i: int = 0
    for x in self:
        if x == item:
            self.pop(i)
            return
        i = i + 1
    raise ValueError("list.remove(x): x not in list")

def list_copy(self) -> list:
    return list(self)
