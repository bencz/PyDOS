"""Python-level shallow and deep copy operations."""


def copy(value):
    method = getattr(value, "__copy__", None)
    if method is not None:
        return method()
    if isinstance(value, list):
        return value.copy()
    if isinstance(value, dict):
        return value.copy()
    if isinstance(value, set):
        return value.copy()
    return value


def deepcopy(value, memo=None):
    if memo is None:
        memo = {}
    existing = memo.get(id(value))
    if existing is not None:
        return existing
    method = getattr(value, "__deepcopy__", None)
    if method is not None:
        return method(memo)
    if isinstance(value, list):
        result = []
        memo[id(value)] = result
        for item in value:
            result.append(deepcopy(item, memo))
        return result
    if isinstance(value, dict):
        result = {}
        memo[id(value)] = result
        for key, item in value.items():
            result[deepcopy(key, memo)] = deepcopy(item, memo)
        return result
    return value
