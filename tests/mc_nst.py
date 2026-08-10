# Test nested patterns in match/case
# Mapping in sequence
def check_records(data) -> None:
    match data:
        case [{"name": n}, {"name": m}]:
            print(n)
            print(m)
        case _:
            print("no match")

check_records([{"name": "Alice"}, {"name": "Bob"}])
check_records([{"name": "Eve"}])

# Sequence in mapping
def check_coords(d) -> None:
    match d:
        case {"pos": [x, y]}:
            print(x)
            print(y)
        case _:
            print("no pos")

check_coords({"pos": [10, 20]})
check_coords({"pos": [1, 2, 3]})

# Literal in sequence
def check_cmd(cmd) -> None:
    match cmd:
        case ["quit"]:
            print("quitting")
        case ["go", direction]:
            print(direction)
        case _:
            print("unknown cmd")

check_cmd(["quit"])
check_cmd(["go", "north"])
check_cmd(["help"])
