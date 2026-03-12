def test_multi_except() -> None:
    # Test catching specific exception type
    try:
        x: int = 1 // 0
    except ZeroDivisionError:
        print("caught ZeroDivisionError")

    # Test catching ValueError
    try:
        raise ValueError("bad value")
    except TypeError:
        print("caught TypeError")
    except ValueError as e:
        print("caught ValueError")

    # Test bare except catches all
    try:
        raise RuntimeError("oops")
    except RuntimeError:
        print("caught RuntimeError")

    # Test Exception catches all standard exceptions
    try:
        raise KeyError("missing")
    except Exception:
        print("caught via Exception")

    print("all multi-except tests done")

def main() -> None:
    test_multi_except()

main()
