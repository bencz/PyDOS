# bytes.py - High-level behavior for the immutable bytes primitive

class bytes:
    def decode(self, encoding: str = "utf-8", errors: str = "strict") -> str:
        if encoding is None:
            encoding = "utf-8"
        if errors is None:
            errors = "strict"

        normalized: str = encoding.lower().replace("_", "-")
        is_utf8: bool = normalized == "utf-8" or normalized == "utf8"
        is_ascii: bool = normalized == "ascii" or normalized == "us-ascii"
        is_latin1: bool = (normalized == "latin-1" or normalized == "latin1" or
                           normalized == "iso-8859-1")
        if not (is_utf8 or is_ascii or is_latin1):
            raise LookupError("unknown encoding: " + encoding)

        result: str = ""
        i: int = 0
        while i < len(self):
            value: int = self[i]
            if is_latin1 or value <= 127:
                result = result + chr(value)
                i = i + 1
            elif is_ascii:
                if errors == "ignore":
                    i = i + 1
                elif errors == "replace":
                    result = result + "?"
                    i = i + 1
                else:
                    raise UnicodeDecodeError("ordinal not in range(128)")
            elif (value >= 194 and value <= 223 and i + 1 < len(self) and
                  self[i + 1] >= 128 and self[i + 1] <= 191):
                result = result + chr((value - 192) * 64 + self[i + 1] - 128)
                i = i + 2
            elif errors == "ignore":
                i = i + 1
            elif errors == "replace":
                result = result + "?"
                i = i + 1
            else:
                raise UnicodeDecodeError("invalid UTF-8 sequence")
        return result

    def hex(self, sep=None, bytes_per_sep: int = 1) -> str:
        digits: str = "0123456789abcdef"
        result: str = ""
        n: int = len(self)
        if bytes_per_sep is None:
            bytes_per_sep = 1
        if bytes_per_sep < 0:
            bytes_per_sep = -bytes_per_sep
        if bytes_per_sep == 0:
            raise ValueError("bytes_per_sep must be non-zero")

        i: int = 0
        while i < n:
            if sep is not None and i > 0 and (n - i) % bytes_per_sep == 0:
                result = result + sep
            value: int = self[i]
            result = result + digits[value // 16] + digits[value % 16]
            i = i + 1
        return result
