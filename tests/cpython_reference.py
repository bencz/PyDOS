"""Execute a PyDOS test under CPython with host implementations of DOS I/O.

The language and high-level stdlib code still execute in CPython.  Only the
primitive functions normally supplied by the C runtime are adapted to the
host operating system.
"""

import builtins
import runpy
import sys
import time


_files = {}
_next_handle = 3


def _file_open(path, mode):
    global _next_handle
    try:
        file_value = open(path, mode, encoding="utf-8", newline=None)
    except OSError:
        return -1
    handle = _next_handle
    _next_handle += 1
    _files[handle] = file_value
    return handle


def _file_read(handle, size):
    return _files[handle].read(size)


def _file_write(handle, value):
    written = _files[handle].write(value)
    _files[handle].flush()
    return written


def _file_close(handle):
    file_value = _files.pop(handle, None)
    if file_value is not None:
        file_value.close()


def _install_runtime_primitives():
    primitives = {
        "_pydos_file_open": _file_open,
        "_pydos_file_read": _file_read,
        "_pydos_file_write": _file_write,
        "_pydos_file_close": _file_close,
        "_pydos_tui_clear": lambda foreground, background: None,
        "_pydos_tui_write_at": (
            lambda x, y, text, foreground, background: None
        ),
        "_pydos_tui_cursor": lambda x, y: None,
        "_pydos_tui_cursor_visible": lambda visible: None,
        "_pydos_tui_key_available": lambda: False,
        "_pydos_tui_read_key": lambda: None,
        "_pydos_tui_ticks_ms": lambda: int(time.monotonic() * 1000),
        "_pydos_tui_delay_ms": lambda milliseconds: time.sleep(
            max(0, milliseconds) / 1000
        ),
        "_pydos_tui_width": lambda: 80,
        "_pydos_tui_height": lambda: 25,
    }
    for name, implementation in primitives.items():
        setattr(builtins, name, implementation)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: cpython_reference.py SOURCE [ENTRY]")
    _install_runtime_primitives()
    source = sys.argv[1]
    entry = sys.argv[2] if len(sys.argv) > 2 else ""
    namespace = runpy.run_path(
        source,
        run_name="__pydos_reference__" if entry else "__main__",
    )
    if entry:
        namespace[entry]()


if __name__ == "__main__":
    main()
