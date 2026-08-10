"""Small runtime-facing subset of :mod:`typing` for compiled programs.

Static annotations are handled by the compiler.  The objects here exist only
for Python 3.12 protocols that deliberately expose runtime metadata.
"""


class TypedDict:
    __orig_bases__ = ()


class Unpack:
    pass


def override(method):
    setattr(method, "__override__", True)
    return method
