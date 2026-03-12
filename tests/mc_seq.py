# Test star-in-sequence patterns
def check_seq(lst) -> None:
    match lst:
        case [first, *middle, last]:
            print(first)
            print(middle)
            print(last)
        case [only]:
            print(only)
        case _:
            print("no match")

# Star captures middle
check_seq([1, 2, 3, 4, 5])

# Star captures empty middle
check_seq([10, 20])

# Single element matches [only]
check_seq([42])

# Test *all pattern
def check_all(lst) -> None:
    match lst:
        case [*items]:
            print(items)

check_all([7, 8, 9])

# Test star with wildcard
def check_head(lst) -> None:
    match lst:
        case [x, *_]:
            print(x)
        case _:
            print("empty")

check_head([100, 200, 300])
check_head([99])
