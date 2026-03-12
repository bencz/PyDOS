# is_inst.py - isinstance() tests

# Test 1: basic builtin types
def test_builtin_types() -> None:
    x: int = 42
    s: str = "hello"
    b: bool = True
    lst: list[int] = [1, 2, 3]
    d: dict[str, int] = {"a": 1}

    print(isinstance(x, int))
    print(isinstance(x, str))
    print(isinstance(s, str))
    print(isinstance(s, int))
    print(isinstance(b, bool))
    print(isinstance(lst, list))
    print(isinstance(d, dict))

# Test 2: bool is subtype of int in Python
def test_bool_int_subtype() -> None:
    b: bool = True
    print(isinstance(b, int))
    f: bool = False
    print(isinstance(f, int))

# Test 3: isinstance in conditionals
def test_isinstance_in_if() -> None:
    val: int = 10
    if isinstance(val, int):
        print("val is int")
    else:
        print("val is not int")

    name: str = "test"
    if isinstance(name, str):
        print("name is str")
    else:
        print("name is not str")

    if isinstance(name, int):
        print("WRONG")
    else:
        print("name is not int")

# Test 4: isinstance with None
def test_none() -> None:
    x: int = 42
    print(isinstance(x, bool))

# Test 5: isinstance in function
def check_type(x: int) -> str:
    if isinstance(x, int):
        return "integer"
    return "other"

def test_function() -> None:
    print(check_type(99))

def main() -> None:
    test_builtin_types()
    test_bool_int_subtype()
    test_isinstance_in_if()
    test_none()
    test_function()
    print("all isinstance tests done")

main()
