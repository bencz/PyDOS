# Test mapping patterns in match/case
def check_map(d) -> None:
    match d:
        case {"name": n, "age": a}:
            print(n)
            print(a)
        case {"type": t}:
            print(t)
        case _:
            print("no match")

# Basic mapping match
check_map({"name": "Alice", "age": 30})

# Extra keys allowed (PEP 634)
check_map({"name": "Bob", "age": 25, "city": "NYC"})

# Partial match: only "type" key
check_map({"type": "circle"})

# No match
check_map({"x": 1})

# Nested mapping pattern
def check_nested(d) -> None:
    match d:
        case {"point": {"x": x, "y": y}}:
            print(x)
            print(y)
        case _:
            print("not a point")

check_nested({"point": {"x": 10, "y": 20}})
check_nested({"point": {"z": 5}})
