"""Non-blocking and blocking keyboard input."""

from pydos.io.tui.clock import delay


def key_available():
    return _pydos_tui_key_available()


def read_key():
    return _pydos_tui_read_key()


def wait_key():
    key = read_key()
    while key is None:
        delay(10)
        key = read_key()
    return key
