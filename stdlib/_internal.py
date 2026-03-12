# _internal.py - Decorator definitions for stdlib stubs
#
# These stubs are the single source of truth for stdlibgen.
# stdlibgen parses these .py files to generate stdlib.idx.

def internal_implementation(cname: str, exc_code: int = -1, fast_argc: int = -1):
    """Marks a function/method as implemented by a C runtime symbol."""
    def decorator(fn):
        return fn
    return decorator

