# Test map builtin (Python-backed via stdlib PIR)
def double(x: int) -> int:
    return x * 2

def negate(x: int) -> int:
    return -x

# Basic usage
for v in map(double, [1, 2, 3]):
    print(v)
# Different function
for v in map(negate, [5, 10, 15]):
    print(v)
# Empty
for v in map(double, []):
    print(v)
# Single element
for v in map(double, [7]):
    print(v)
