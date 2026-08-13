"""Helper module for the dead-code-elimination tests (dce_bas, dce_vtb).

Linked by source into both tests.  unused_function and UnusedClass are
referenced by nobody and must be removed by the AST DCE pass without
changing any observable output; everything else is reachable through a
direct call, an import alias, a base class, a transitive helper call or
a decorator side effect.
"""


def used_function(value: int) -> int:
    return value * 2


def helper_only(value: int) -> int:
    return value + 3


def transitive_helper(value: int) -> int:
    return helper_only(value) * 10


def unused_function(value: int) -> int:
    return value - 999


def aliased_helper() -> str:
    return "via-alias"


def register(fn):
    print("decorator ran")
    return fn


@register
def decorated_unused() -> str:
    return "kept-and-registered"


class UnusedClass:
    def __init__(self) -> None:
        self.tag = "never-created"


class BaseShape:
    def describe(self) -> str:
        return "base-shape"


class Circle(BaseShape):
    def __init__(self, radius: int) -> None:
        self.radius = radius

    def area_times_ten(self) -> int:
        return transitive_helper(self.radius)


class Wrapped:
    def __init__(self, value: int) -> None:
        self.value = value

    def __str__(self) -> str:
        return "Wrapped(" + str(self.value) + ")"

    def __eq__(self, other) -> bool:
        return self.value == other.value

    def __lt__(self, other) -> bool:
        return self.value < other.value

    def __len__(self) -> int:
        return self.value
