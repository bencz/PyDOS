# Test: Tuple operations
# Tests tuple creation, indexing, iteration, len, contains, to_str

def test_tuples() -> None:
    # Basic tuple creation and printing
    t: tuple = (1, 2, 3)
    print(t)

    # Tuple indexing
    print(t[0])
    print(t[1])
    print(t[2])

    # Negative indexing
    print(t[-1])

    # Tuple with strings
    names: tuple = ("alice", "bob", "charlie")
    print(names[0])
    print(names[1])

    # Len
    print(len(t))
    print(len(names))

    # Contains
    if 2 in t:
        print("2 in tuple")
    if 5 not in t:
        print("5 not in tuple")

    # Iteration
    total: int = 0
    x: int = 0
    for x in t:
        total = total + x
    print(total)

    # Mixed tuple
    m: tuple = (42, "hello", True)
    print(m[0])
    print(m[1])
    print(m[2])

    # Single-element tuple
    s: tuple = (99,)
    print(s)
    print(len(s))

test_tuples()
