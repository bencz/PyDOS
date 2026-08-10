"""DOS monotonic-clock and cooperative delay helpers."""


def delay(milliseconds):
    _pydos_tui_delay_ms(milliseconds)


def ticks_ms():
    return _pydos_tui_ticks_ms()
