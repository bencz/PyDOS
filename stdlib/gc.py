"""Cycle-collector controls for PyDOS.

PyDOS has one compact, non-generational cycle collector.  The generation
argument is accepted for Python 3.12 source compatibility, but every request
runs a full collection.
"""


def collect(generation=2):
    if generation < 0 or generation > 2:
        raise ValueError("invalid generation")
    return _pydos_gc_collect()


def is_tracked(obj):
    return _pydos_gc_is_tracked(obj)


def enable():
    _pydos_gc_control(0)


def disable():
    _pydos_gc_control(1)


def isenabled():
    return _pydos_gc_control(2)


def get_count():
    return (_pydos_gc_control(3), 0, 0)


def get_threshold():
    return (
        _pydos_gc_control(7),
        _pydos_gc_control(9),
        _pydos_gc_control(10),
    )


def set_threshold(threshold0, threshold1=10, threshold2=10):
    if threshold0 < 0 or threshold1 < 0 or threshold2 < 0:
        raise ValueError("thresholds must be non-negative")
    if threshold0 > 65535 or threshold1 > 65535 or threshold2 > 65535:
        raise OverflowError("thresholds must fit in 16 bits")
    _pydos_gc_control(8, threshold0, threshold1, threshold2)


def get_stats():
    return [
        {
            "collections": _pydos_gc_control(4),
            "collected": _pydos_gc_control(5),
            "uncollectable": 0,
        },
        {"collections": 0, "collected": 0, "uncollectable": 0},
        {"collections": 0, "collected": 0, "uncollectable": 0},
    ]
