"""Basic async/await test."""

async def add(a: int, b: int) -> int:
    return a + b

async def greet(name: str) -> str:
    return "Hello " + name

async def chain() -> None:
    x: int = await add(1, 2)
    print(x)
    msg: str = await greet("World")
    print(msg)

async def nested() -> int:
    a: int = await add(10, 20)
    b: int = await add(a, 5)
    return b

async def main() -> None:
    await chain()
    r: int = await nested()
    print(r)
    async def trivial() -> int:
        return 42
    t: int = await trivial()
    print(t)

print(async_run(main()))
