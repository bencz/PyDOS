"""Cooperative scheduling with async_gather."""

from asyncio import gather, run, sleep

async def worker(name: str, n: int) -> str:
    i: int = 0
    while i < n:
        print(name + " " + str(i))
        await sleep(0)
        i = i + 1
    return name + " done"

async def main() -> None:
    results: list = await gather(worker("A", 3), worker("B", 2))
    x: object
    for x in results:
        print(x)

run(main())
