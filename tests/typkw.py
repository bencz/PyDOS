from typing import TypedDict, Unpack


class RequestOptions(TypedDict):
    timeout: float
    retries: int


def typed_request(**options: Unpack[RequestOptions]) -> RequestOptions:
    return options


def configure_request(**options):
    return options


def mixed(first, *, enabled=False, **extras):
    return first, enabled, extras


print(configure_request(timeout=1.5, retries=3))
print(typed_request(timeout=1.5, retries=3))
print("options" in typed_request.__annotations__)
print("kwargs" not in typed_request.__annotations__)
print("Unpack" in repr(typed_request.__annotations__["options"]))
print(mixed(10, enabled=True, label="x", count=2))
