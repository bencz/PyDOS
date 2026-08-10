# Test filter builtin (Python-backed via stdlib PIR)
def is_even(x: int) -> bool:
    return x % 2 == 0

def is_positive(x: int) -> bool:
    return x > 0

# Basic usage
for v in filter(is_even, [1, 2, 3, 4, 5, 6]):
    print(v)
# Different predicate
for v in filter(is_positive, [-2, -1, 0, 1, 2]):
    print(v)
# None match
for v in filter(is_even, [1, 3, 5]):
    print(v)
# All match
for v in filter(is_even, [2, 4]):
    print(v)
# Empty
for v in filter(is_even, []):
    print(v)
