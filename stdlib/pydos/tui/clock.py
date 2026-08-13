"""Time services over the BIOS tick counter.

``ticks_ms`` counts milliseconds since midnight in ~55 ms steps and
wraps daily; ``sleep_ms`` waits cooperatively (the C side yields the
time slice to the host).  ``FrameClock`` paces a game loop:

    clock = FrameClock(55)
    while running:
        update()
        draw()
        clock.wait()
"""

_TICK_DAY_MS: int = 86517200


def ticks_ms() -> int:
    return _pydos_tui_ticks_ms()


def sleep_ms(milliseconds: int) -> None:
    _pydos_tui_sleep_ms(milliseconds)


class FrameClock:
    def __init__(self, interval_ms: int) -> None:
        self.interval_ms = interval_ms
        self.last = ticks_ms()

    def wait(self) -> None:
        """Sleep whatever remains of the current frame interval."""
        now: int = ticks_ms()
        elapsed: int = now - self.last
        if elapsed < 0:
            elapsed = _TICK_DAY_MS - self.last + now
        remaining: int = self.interval_ms - elapsed
        if remaining > 0:
            sleep_ms(remaining)
        self.last = ticks_ms()
