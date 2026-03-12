def sum_all(first: int, *rest: int) -> int:
    result: int = first
    i: int = 0
    while i < len(rest):
        result = result + rest[i]
        i = i + 1
    return result

def show_kwargs(**opts: str) -> None:
    keys: list = opts.keys()
    i: int = 0
    while i < len(keys):
        print(keys[i])
        i = i + 1

def mixed(a: int, *args: int, **kwargs: int) -> None:
    print(a)
    print(len(args))
    print(len(kwargs))

def only_star(*nums: int) -> int:
    total: int = 0
    i: int = 0
    while i < len(nums):
        total = total + nums[i]
        i = i + 1
    return total

def main() -> None:
    r: int = sum_all(1, 2, 3, 4)
    print(r)

    r = sum_all(10)
    print(r)

    r = only_star(5, 10, 15)
    print(r)

    r = only_star()
    print(r)

    show_kwargs(x="hello", y="world")

    mixed(42, 10, 20, name=100, value=200)

main()
