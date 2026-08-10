"""Private bridges to DOS file-handle primitives."""

from _internal import internal_implementation


@internal_implementation("pydos_io_file_open_")
def _pydos_file_open(path: str, mode: str = "r") -> int: ...


@internal_implementation("pydos_io_file_read_")
def _pydos_file_read(handle: int, count: int) -> str: ...


@internal_implementation("pydos_io_file_write_")
def _pydos_file_write(handle: int, data: str) -> int: ...


@internal_implementation("pydos_io_file_close_")
def _pydos_file_close(handle: int) -> None: ...
