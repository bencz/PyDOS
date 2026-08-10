"""Public file-opening and convenience functions."""

from pydos.io.text import TextFile, TextIOWrapper


def _validate_text_options(mode, buffering, encoding, closefd, opener):
    if encoding is None:
        encoding = "utf-8"
    if encoding != "utf-8" and encoding != "utf8" and encoding != "ascii":
        raise ValueError("supported encodings are utf-8 and ascii")
    if buffering == 0:
        raise ValueError("unbuffered text I/O is not supported")
    if not closefd:
        raise ValueError("closefd=False is not supported")
    if opener is not None:
        raise ValueError("custom openers are not supported")
    if "b" in mode:
        raise ValueError("binary file mode is not implemented yet")
    if mode != "r" and mode != "w" and mode != "r+":
        raise ValueError("supported file modes are 'r', 'w' and 'r+'")
    return encoding


def open(path, mode="r", buffering=-1, encoding=None, errors=None,
         newline=None, closefd=True, opener=None):
    encoding = _validate_text_options(mode, buffering, encoding, closefd,
                                      opener)
    handle = _pydos_file_open(path, mode)
    if handle < 0:
        if mode == "r":
            raise FileNotFoundError(path)
        raise OSError(path)
    return TextFile(handle, path, mode, encoding, errors, newline)


def open_file(path, mode="r", encoding=None):
    """Compatibility alias retained for existing PyDOS applications."""
    return open(path, mode, encoding=encoding)


def read_text(path, encoding=None):
    with open(path, "r", encoding=encoding) as file_value:
        result = file_value.read()
    return result


def write_text(path, data, encoding=None):
    with open(path, "w", encoding=encoding) as file_value:
        result = file_value.write(data)
    return result
