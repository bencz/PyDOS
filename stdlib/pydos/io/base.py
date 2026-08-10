"""Common stream lifecycle behavior."""


class IOBase:
    """Small Python-level equivalent of the standard ``io.IOBase``."""

    def __init__(self, handle, name, mode):
        self.handle = handle
        self.name = name
        self.mode = mode
        self._closed = False

    @property
    def closed(self):
        return self._closed

    def _check_closed(self):
        if self._closed:
            raise ValueError("I/O operation on closed file")

    def close(self):
        if not self._closed:
            _pydos_file_close(self.handle)
            self._closed = True

    def flush(self):
        self._check_closed()

    def fileno(self):
        self._check_closed()
        return self.handle

    def readable(self):
        return False

    def writable(self):
        return False

    def seekable(self):
        return False

    def isatty(self):
        return False

    def __enter__(self):
        self._check_closed()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.close()
        return False
