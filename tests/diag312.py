class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x, y):
        self.x = x
        self.y = y


def describe(value):
    match value:
        case Point(0, 0):
            return "origin"
        case Point(x, y) if x == y:
            return f"diagonal:{x}"
        case (first, second, *remaining):
            return f"tuple:{first}:{second}:{len(remaining)}"
        case {"name": str(name), "active": True}:
            return f"active:{name}"
        case _:
            return "unknown"


def comprehension_values():
    outer_value = "visible"
    item = "outside"
    snapshots = [
        (item, locals().get("outer_value"), "item" in locals())
        for item in (10, 20)
    ]
    doubled = [result for number in range(6) if (result := number * 2) > 4]
    pairs = list(zip(("a", "b"), (1, 2), strict=True))
    mapping = {key: value for key, value in zip(("a", "b"), (1, 2), strict=True)}
    flattened = [value for group in ((1, 2), (3, 4)) for value in group]
    return item, snapshots, doubled, result, pairs, mapping, flattened


print(comprehension_values())
print(describe(Point(0, 0)))
print(describe(Point(4, 4)))
print(describe((1, 2, 3, 4)))
print(describe({"name": "Ada", "active": True}))
