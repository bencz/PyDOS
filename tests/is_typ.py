# is_typ.py - isinstance() uses registry-based PYDT_* tags
# Regression test: verifies isinstance resolves type tags from
# stdlib.idx rather than hardcoded pirbld.cpp values.

def main() -> None:
    x: int = 42
    s: str = "hello"
    b: bool = True
    f: float = 3.14
    lst: list[int] = [1, 2]
    d: dict[str, int] = {"a": 1}

    # Basic type checks via registry
    print(isinstance(x, int))
    print(isinstance(s, str))
    print(isinstance(b, bool))
    print(isinstance(f, float))
    print(isinstance(lst, list))
    print(isinstance(d, dict))

    # Cross-type negative checks
    print(isinstance(x, str))
    print(isinstance(s, int))
    print(isinstance(f, int))

    # bool is subtype of int
    print(isinstance(b, int))

    print("done")

main()
