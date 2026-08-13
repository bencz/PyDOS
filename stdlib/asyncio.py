"""Small Python 3.12 compatible facade over the PyDOS async scheduler."""


def run(coroutine):
    return async_run(coroutine)


async def gather(*coroutines):
    return async_gather(list(coroutines))


async def sleep(delay, result=None):
    if delay > 0:
        milliseconds = int(delay * 1000)
        _pydos_tui_sleep_ms(milliseconds)
    await None
    return result
