"""Cooperative scheduling with async_gather."""

async def worker(name: str, n: int) -> str:
    i: int = 0
    while i < n:
        print(name + " " + str(i))
        await None
        i = i + 1
    return name + " done"

async def main() -> None:
    results: list = async_gather([worker("A", 3), worker("B", 2)])
    x: object
    for x in results:
        print(x)

async_run(main())
