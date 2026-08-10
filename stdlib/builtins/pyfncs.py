# pyfncs.py - Python-backed module-level builtin implementations
#
# Class methods live in their corresponding type modules (list.py, str.py,
# and so on).  This file contains only functions declared by funcs.py.

def any(iterable) -> bool:
    for item in iterable:
        if item:
            return True
    return False

def all(iterable) -> bool:
    for item in iterable:
        if not item:
            return False
    return True

def sum(iterable, start=0) -> object:
    result = start
    for item in iterable:
        result = result + item
    return result

def min(a, b=None, key=None) -> object:
    if b is not None:
        if key is None:
            if a < b:
                return a
            return b
        if key(a) < key(b):
            return a
        return b

    found: bool = False
    result = None
    result_key = None
    for item in a:
        if key is None:
            item_key = item
        else:
            item_key = key(item)
        if not found or item_key < result_key:
            found = True
            result = item
            result_key = item_key
    if not found:
        raise ValueError("min() arg is an empty sequence")
    return result

def max(a, b=None, key=None) -> object:
    if b is not None:
        if key is None:
            if a > b:
                return a
            return b
        if key(a) > key(b):
            return a
        return b

    found: bool = False
    result = None
    result_key = None
    for item in a:
        if key is None:
            item_key = item
        else:
            item_key = key(item)
        if not found or item_key > result_key:
            found = True
            result = item
            result_key = item_key
    if not found:
        raise ValueError("max() arg is an empty sequence")
    return result

def enumerate(iterable, start: int = 0) -> object:
    i: int = start
    for item in iterable:
        yield (i, item)
        i = i + 1

def zip(a, b, strict=False) -> object:
    i: int = 0
    la: int = len(a)
    lb: int = len(b)
    n: int = la
    if lb < la:
        n = lb
    while i < n:
        yield (a[i], b[i])
        i = i + 1
    if strict and la != lb:
        raise ValueError("zip() arguments have different lengths")

def map(func, iterable) -> object:
    for item in iterable:
        yield func(item)

def filter(func, iterable) -> object:
    for item in iterable:
        if func is None:
            if item:
                yield item
        else:
            if func(item):
                yield item

def sorted(iterable, key=None, reverse=False) -> list:
    result: list = list(iterable)
    i: int = 1
    while i < len(result):
        current = result[i]
        if key is not None:
            current_key = key(current)
        else:
            current_key = current

        j: int = i - 1
        while j >= 0:
            other = result[j]
            if key is not None:
                other_key = key(other)
            else:
                other_key = other

            move: bool = False
            if reverse:
                move = other_key < current_key
            else:
                move = other_key > current_key
            if not move:
                break

            result[j + 1] = other
            j = j - 1

        result[j + 1] = current
        i = i + 1
    return result

def reversed(seq) -> object:
    custom = getattr(seq, "__reversed__", None)
    if custom is not None:
        for item in custom():
            yield item
        return
    i: int = len(seq) - 1
    while i >= 0:
        yield seq[i]
        i = i - 1

def format(value, format_spec: str = "") -> str:
    custom = getattr(value, "__format__", None)
    if custom is not None:
        return custom(format_spec)
    if format_spec:
        raise TypeError("unsupported format string")
    return str(value)

def bin(number: int) -> str:
    if number == 0:
        return "0b0"
    prefix: str = "0b"
    value: int = number
    if value < 0:
        prefix = "-0b"
        value = -value
    digits: str = ""
    while value > 0:
        digits = str(value % 2) + digits
        value = value // 2
    return prefix + digits

def oct(number: int) -> str:
    if number == 0:
        return "0o0"
    prefix: str = "0o"
    value: int = number
    if value < 0:
        prefix = "-0o"
        value = -value
    digits: str = "01234567"
    result: str = ""
    while value > 0:
        result = digits[value % 8] + result
        value = value // 8
    return prefix + result

def divmod(a, b) -> tuple:
    return (a // b, a % b)

def pow(base, exp, mod=None) -> object:
    if mod is None:
        return base ** exp
    if mod == 0:
        raise ValueError("pow() 3rd argument cannot be 0")

    exponent: int = exp
    factor: int = base
    if exponent < 0:
        old_r: int = factor
        r: int = mod
        old_s: int = 1
        s: int = 0
        while r != 0:
            quotient: int = old_r // r
            next_r: int = old_r - quotient * r
            old_r = r
            r = next_r
            next_s: int = old_s - quotient * s
            old_s = s
            s = next_s
        if old_r == -1:
            old_s = -old_s
            old_r = 1
        if old_r != 1:
            raise ValueError("base is not invertible for the given modulus")
        factor = old_s % mod
        exponent = -exponent

    result: int = 1 % mod
    factor = factor % mod
    while exponent > 0:
        if exponent % 2 == 1:
            result = (result * factor) % mod
        exponent = exponent // 2
        if exponent > 0:
            factor = (factor * factor) % mod
    return result
