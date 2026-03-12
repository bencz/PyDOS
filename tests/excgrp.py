# Test except* (exception groups)

# Basic except* — catch ValueError from group
def test_basic() -> None:
    try:
        raise ExceptionGroup("errors", [ValueError("bad val"), TypeError("bad type")])
    except* ValueError as eg:
        print("caught ValueError group")
    except* TypeError as eg:
        print("caught TypeError group")

test_basic()

# except* with single match — remainder re-raised as ExceptionGroup
def test_partial() -> None:
    try:
        try:
            raise ExceptionGroup("mixed", [ValueError("v1"), KeyError("k1")])
        except* ValueError as eg:
            print("caught ValueError")
    except Exception as e:
        print("caught remaining group")

test_partial()

# except* with all matching
def test_all_match() -> None:
    try:
        raise ExceptionGroup("all vals", [ValueError("a"), ValueError("b")])
    except* ValueError as eg:
        print("caught all ValueErrors")

test_all_match()
