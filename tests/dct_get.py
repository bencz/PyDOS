# Test: dict.get(key, default) method
# Tests dict.get() with and without default values

def test_dict_get() -> None:
    d: dict = {"a": 1, "b": 2, "c": 3}

    # Get existing key
    val: int = d.get("a", 0)
    print(val)

    # Get existing key (different value)
    val = d.get("c", 0)
    print(val)

    # Get missing key with default
    val = d.get("z", 42)
    print(val)

    # Get missing key with default 0
    val = d.get("missing", 0)
    print(val)

test_dict_get()
