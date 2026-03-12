# Test: Set operations
# Tests set creation, len, in/not in, and iteration

def test_set_basic() -> None:
    s: set = {10, 20, 30}
    print(len(s))       # 3

    # Membership tests
    print(10 in s)       # True
    print(20 in s)       # True
    print(99 in s)       # False
    print(99 not in s)   # True

def test_set_iteration() -> None:
    s: set = {5, 15, 25}
    total: int = 0
    for x in s:
        total = total + x
    print(total)         # 45

def test_set_empty() -> None:
    # Empty set via set literal not possible in Python (that's dict)
    # but we can test len/truthiness on a populated set
    s: set = {1}
    print(len(s))        # 1
    if s:
        print("truthy")  # truthy

def test_set_strings() -> None:
    s: set = {"hello", "world"}
    print("hello" in s)  # True
    print("xyz" in s)    # False
    print(len(s))        # 2

test_set_basic()
test_set_iteration()
test_set_empty()
test_set_strings()
