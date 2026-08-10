"""Buffered text streams implemented in Python."""

from pydos.io.base import IOBase


class TextFile(IOBase):
    """DOS text file with Python context-manager and iterator protocols."""

    def __init__(self, handle, path, mode, encoding="utf-8", errors=None,
                 newline=None):
        super().__init__(handle, path, mode)
        self.path = path
        self.encoding = encoding
        self.errors = errors
        self.newline = newline
        self._read_buffer = ""

    def readable(self):
        return not self.closed and self.mode[0] == "r"

    def writable(self):
        return (not self.closed
                and (self.mode[0] == "w" or "+" in self.mode))

    def _check_readable(self):
        self._check_closed()
        if not self.readable():
            raise OSError("file is not open for reading")

    def _check_writable(self):
        self._check_closed()
        if not self.writable():
            raise OSError("file is not open for writing")

    def read(self, size=-1):
        self._check_readable()
        if size >= 0:
            result = self._read_buffer[:size]
            self._read_buffer = self._read_buffer[size:]
            remaining = size - len(result)
            if remaining > 0:
                result += _pydos_file_read(self.handle, remaining)
            return result

        result = self._read_buffer
        self._read_buffer = ""
        chunk = _pydos_file_read(self.handle, 1024)
        while len(chunk) > 0:
            result += chunk
            chunk = _pydos_file_read(self.handle, 1024)
        return result

    def readline(self, size=-1):
        self._check_readable()
        while True:
            newline_at = self._read_buffer.find("\n")
            if newline_at >= 0:
                take = newline_at + 1
                if size >= 0 and size < take:
                    take = size
                result = self._read_buffer[:take]
                self._read_buffer = self._read_buffer[take:]
                return result
            if size >= 0 and len(self._read_buffer) >= size:
                result = self._read_buffer[:size]
                self._read_buffer = self._read_buffer[size:]
                return result
            chunk = _pydos_file_read(self.handle, 256)
            if len(chunk) == 0:
                result = self._read_buffer
                self._read_buffer = ""
                return result
            self._read_buffer += chunk

    def readlines(self):
        result = []
        line = self.readline()
        while line != "":
            result.append(line)
            line = self.readline()
        return result

    def write(self, data):
        self._check_writable()
        text = str(data)
        written = _pydos_file_write(self.handle, text)
        if written != len(text):
            raise OSError("could not write the complete file data")
        return written

    def writelines(self, lines):
        written = 0
        for line in lines:
            written += self.write(line)
        return written

    def __iter__(self):
        return self

    def __next__(self):
        line = self.readline()
        if line == "":
            raise StopIteration()
        return line


TextIOWrapper = TextFile
