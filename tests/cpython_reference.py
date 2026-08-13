"""Execute a PyDOS test under CPython with host implementations of DOS I/O.

The language and high-level stdlib code still execute in CPython.  Only the
primitive functions normally supplied by the C runtime are adapted to the
host operating system.
"""

import builtins
import os
import runpy
import sys
import time


def _demote_pydos_stdlib_entries():
    """Move PyDOS stdlib roots to the end of sys.path.

    The PyDOS stdlib ships minimal collections/abc/typing/... written for
    the DOS compiler.  On PYTHONPATH they shadow CPython's real modules
    (runpy itself dies importing pkgutil -> collections.namedtuple).  The
    reference run must resolve those names to CPython's implementations —
    that is the whole point of a golden output — while ``pydos.*``, which
    exists only in the PyDOS stdlib, keeps resolving.  Ordering the PyDOS
    entry after CPython's own library paths achieves both.
    """
    demoted = [
        entry for entry in sys.path
        if os.path.isfile(os.path.join(entry, "pydos", "__init__.py"))
    ]
    for entry in demoted:
        sys.path.remove(entry)
    sys.path.extend(demoted)


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
        "_pydos_tui_cursor": lambda x, y: None,
        "_pydos_tui_ticks_ms": lambda: int(time.monotonic() * 1000),
        # Engine primitives (headless like the C host build: no video,
        # no input, cooperative sleep).
        "_pydos_tui_probe": lambda: 80 | (25 << 8),
        "_pydos_tui_present": lambda glyphs, attrs, x, y: None,
        "_pydos_tui_fill": lambda x, y, w, h, ch, attr: None,
        "_pydos_tui_scroll": lambda x, y, w, h, lines, attr: None,
        "_pydos_tui_cursor_shape": lambda kind: None,
        "_pydos_tui_set_rows": lambda rows: rows,
        "_pydos_tui_blink": lambda enabled: None,
        "_pydos_tui_save_video": lambda: 3 | (25 << 8) | (6 << 16)
        | (7 << 23),
        "_pydos_tui_restore_video": lambda state: None,
        "_pydos_tui_vsync": lambda: None,
        "_pydos_tui_key_event": lambda: -1,
        "_pydos_tui_shift_state": lambda: 0,
        "_pydos_tui_mouse_init": lambda: 0,
        "_pydos_tui_mouse_poll": lambda: -1,
        "_pydos_tui_mouse_show": lambda visible: None,
        "_pydos_tui_sleep_ms": lambda milliseconds: time.sleep(
            max(0, milliseconds) / 1000
        ),
        "_pydos_dir_first": lambda pattern: "",
        "_pydos_dir_next": lambda: "",
    }
    for name, implementation in primitives.items():
        setattr(builtins, name, implementation)


def main():
    if len(sys.argv) < 2:
        raise SystemExit("usage: cpython_reference.py SOURCE [ENTRY]")
    _demote_pydos_stdlib_entries()
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
