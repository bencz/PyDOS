# str.py - Python string class for PyDOS
#
# C owns string representation and primitive indexing/slicing operations.
# User-visible algorithms are implemented here in Python and serialized to PIR.

from _internal import internal_implementation

class str:
    @internal_implementation("pydos_str_index_op_")
    def __getitem__(self, index: int):
        pass

    @internal_implementation("pydos_str_slice_op_")
    def __getslice__(self, start: int, stop: int, step: int):
        pass

    @internal_implementation("pydos_obj_contains_")
    def __contains__(self, sub: str) -> bool:
        pass

    def upper(self) -> str:
        result: str = ""
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if code >= 97 and code <= 122:
                result = result + chr(code - 32)
            else:
                result = result + self[i]
            i = i + 1
        return result

    def lower(self) -> str:
        result: str = ""
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if code >= 65 and code <= 90:
                result = result + chr(code + 32)
            else:
                result = result + self[i]
            i = i + 1
        return result

    def strip(self, chars=None) -> str:
        start: int = 0
        end: int = len(self)
        while start < end:
            if chars is None:
                left_code: int = ord(self[start])
                if not (left_code == 32 or left_code == 9 or left_code == 10 or
                        left_code == 13 or left_code == 12 or left_code == 11):
                    break
            elif self[start] not in chars:
                break
            start = start + 1
        while end > start:
            if chars is None:
                right_code: int = ord(self[end - 1])
                if not (right_code == 32 or right_code == 9 or right_code == 10 or
                        right_code == 13 or right_code == 12 or right_code == 11):
                    break
            elif self[end - 1] not in chars:
                break
            end = end - 1
        return self[start:end]

    def lstrip(self, chars=None) -> str:
        start: int = 0
        n: int = len(self)
        while start < n:
            if chars is None:
                code: int = ord(self[start])
                if not (code == 32 or code == 9 or code == 10 or code == 13 or
                        code == 12 or code == 11):
                    break
            elif self[start] not in chars:
                break
            start = start + 1
        return self[start:n]

    def rstrip(self, chars=None) -> str:
        end: int = len(self)
        while end > 0:
            if chars is None:
                code: int = ord(self[end - 1])
                if not (code == 32 or code == 9 or code == 10 or code == 13 or
                        code == 12 or code == 11):
                    break
            elif self[end - 1] not in chars:
                break
            end = end - 1
        return self[0:end]

    def title(self) -> str:
        result: str = ""
        at_word_start: bool = True
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            is_lower: bool = code >= 97 and code <= 122
            is_upper: bool = code >= 65 and code <= 90
            is_digit: bool = code >= 48 and code <= 57
            if at_word_start and is_lower:
                result = result + chr(code - 32)
            elif not at_word_start and is_upper:
                result = result + chr(code + 32)
            else:
                result = result + self[i]
            at_word_start = not (is_lower or is_upper or is_digit)
            i = i + 1
        return result

    def capitalize(self) -> str:
        result: str = ""
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if i == 0 and code >= 97 and code <= 122:
                result = result + chr(code - 32)
            elif i > 0 and code >= 65 and code <= 90:
                result = result + chr(code + 32)
            else:
                result = result + self[i]
            i = i + 1
        return result

    def swapcase(self) -> str:
        result: str = ""
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if code >= 97 and code <= 122:
                result = result + chr(code - 32)
            elif code >= 65 and code <= 90:
                result = result + chr(code + 32)
            else:
                result = result + self[i]
            i = i + 1
        return result

    def replace(self, old: str, new: str, count: int = -1) -> str:
        result: str = ""
        source_len: int = len(self)
        old_len: int = len(old)
        i: int = 0
        replacements: int = 0

        if count is None:
            count = -1
        if count == 0:
            return self[0:source_len]

        if old_len == 0:
            while i <= source_len:
                if count < 0 or replacements < count:
                    result = result + new
                    replacements = replacements + 1
                if i < source_len:
                    result = result + self[i]
                i = i + 1
            return result

        while i < source_len:
            if ((count < 0 or replacements < count) and
                    i <= source_len - old_len and
                    self[i:i + old_len] == old):
                result = result + new
                i = i + old_len
                replacements = replacements + 1
            else:
                result = result + self[i]
                i = i + 1
        return result

    def join(self, iterable) -> str:
        result: str = ""
        separator: str = self[0:len(self)]
        first: bool = True
        for item in iterable:
            if not first:
                result = result + separator
            result = result + item
            first = False
        return result

    def encode(self, encoding: str = "utf-8", errors: str = "strict") -> bytes:
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

        result: list = []
        i: int = 0
        while i < len(self):
            code: int = ord(self[i])
            if is_ascii:
                if code <= 127:
                    result.append(code)
                elif errors == "ignore":
                    pass
                elif errors == "replace":
                    result.append(63)
                else:
                    raise UnicodeEncodeError("ordinal not in range(128)")
            elif is_latin1 or code <= 127:
                result.append(code)
            else:
                result.append(192 + (code // 64))
                result.append(128 + (code % 64))
            i = i + 1
        return bytes(result)

    def center(self, width: int, fillchar: str = " ") -> str:
        if fillchar is None:
            fillchar = " "
        if len(fillchar) != 1:
            raise TypeError("The fill character must be exactly one character long")
        source_len: int = len(self)
        if width <= source_len:
            return self[0:source_len]
        padding: int = width - source_len
        left: int = padding // 2
        if padding % 2 == 1 and width % 2 == 1:
            left = left + 1
        right: int = padding - left
        left_pad: str = ""
        right_pad: str = ""
        i: int = 0
        while i < left:
            left_pad = left_pad + fillchar
            i = i + 1
        i = 0
        while i < right:
            right_pad = right_pad + fillchar
            i = i + 1
        return left_pad + self[0:source_len] + right_pad

    def ljust(self, width: int, fillchar: str = " ") -> str:
        if fillchar is None:
            fillchar = " "
        if len(fillchar) != 1:
            raise TypeError("The fill character must be exactly one character long")
        source_len: int = len(self)
        if width <= source_len:
            return self[0:source_len]
        padding: str = ""
        i: int = source_len
        while i < width:
            padding = padding + fillchar
            i = i + 1
        return self[0:source_len] + padding

    def rjust(self, width: int, fillchar: str = " ") -> str:
        if fillchar is None:
            fillchar = " "
        if len(fillchar) != 1:
            raise TypeError("The fill character must be exactly one character long")
        source_len: int = len(self)
        if width <= source_len:
            return self[0:source_len]
        padding: str = ""
        i: int = source_len
        while i < width:
            padding = padding + fillchar
            i = i + 1
        return padding + self[0:source_len]

    def zfill(self, width: int) -> str:
        source_len: int = len(self)
        if width <= source_len:
            return self[0:source_len]

        pad: int = width - source_len
        zeros: str = ""
        i: int = 0
        while i < pad:
            zeros = zeros + "0"
            i = i + 1

        if source_len > 0 and (self[0] == "+" or self[0] == "-"):
            return self[0] + zeros + self[1:source_len]
        return zeros + self[0:source_len]

    def find(self, sub: str, start: int = 0, end=None) -> int:
        source_len: int = len(self)
        sub_len: int = len(sub)
        if start is None:
            start = 0
        if end is None:
            end = source_len
        if start < 0:
            start = start + source_len
            if start < 0:
                start = 0
        if end < 0:
            end = end + source_len
            if end < 0:
                end = 0
        if end > source_len:
            end = source_len
        if sub_len == 0:
            if start <= end and start <= source_len:
                return start
            return -1
        i: int = start
        while i <= end - sub_len:
            if self[i:i + sub_len] == sub:
                return i
            i = i + 1
        return -1

    def rfind(self, sub: str, start: int = 0, end=None) -> int:
        source_len: int = len(self)
        sub_len: int = len(sub)
        if start is None:
            start = 0
        if end is None:
            end = source_len
        if start < 0:
            start = start + source_len
            if start < 0:
                start = 0
        if end < 0:
            end = end + source_len
            if end < 0:
                end = 0
        if end > source_len:
            end = source_len
        if sub_len == 0:
            if start <= end and start <= source_len:
                return end
            return -1
        i: int = end - sub_len
        while i >= start:
            if self[i:i + sub_len] == sub:
                return i
            i = i - 1
        return -1

    def index(self, sub: str, start: int = 0, end=None) -> int:
        i: int = self.find(sub, start, end)
        if i >= 0:
            return i
        raise ValueError("substring not found")

    def rindex(self, sub: str, start: int = 0, end=None) -> int:
        i: int = self.rfind(sub, start, end)
        if i >= 0:
            return i
        raise ValueError("substring not found")

    def count(self, sub: str, start: int = 0, end=None) -> int:
        source_len: int = len(self)
        sub_len: int = len(sub)
        if start is None:
            start = 0
        if end is None:
            end = source_len
        if start < 0:
            start = start + source_len
            if start < 0:
                start = 0
        if end < 0:
            end = end + source_len
            if end < 0:
                end = 0
        if end > source_len:
            end = source_len
        if sub_len == 0:
            if start <= end and start <= source_len:
                return end - start + 1
            return 0

        count: int = 0
        i: int = start
        while i <= end - sub_len:
            if self[i:i + sub_len] == sub:
                count = count + 1
                i = i + sub_len
            else:
                i = i + 1
        return count

    def split(self, sep=None, maxsplit: int = -1) -> list:
        result: list = []
        n: int = len(self)
        if maxsplit is None:
            maxsplit = -1

        if sep is None:
            i: int = 0
            while i < n:
                code: int = ord(self[i])
                if not (code == 32 or code == 9 or code == 10 or
                        code == 13 or code == 12 or code == 11):
                    break
                i = i + 1
            if i == n:
                return result
            if maxsplit == 0:
                result.append(self[i:n])
                return result

            splits: int = 0
            while i < n:
                if maxsplit >= 0 and splits >= maxsplit:
                    result.append(self[i:n])
                    return result
                start: int = i
                while i < n:
                    code = ord(self[i])
                    if code == 32 or code == 9 or code == 10 or code == 13 or code == 12 or code == 11:
                        break
                    i = i + 1
                result.append(self[start:i])
                splits = splits + 1
                while i < n:
                    code = ord(self[i])
                    if not (code == 32 or code == 9 or code == 10 or
                            code == 13 or code == 12 or code == 11):
                        break
                    i = i + 1
            return result

        sep_len: int = len(sep)
        if sep_len == 0:
            raise ValueError("empty separator")
        start = 0
        splits = 0
        i = 0
        while i <= n - sep_len:
            if (maxsplit < 0 or splits < maxsplit) and self[i:i + sep_len] == sep:
                result.append(self[start:i])
                i = i + sep_len
                start = i
                splits = splits + 1
            else:
                i = i + 1
        result.append(self[start:n])
        return result

    def rsplit(self, sep=None, maxsplit: int = -1) -> list:
        result: list = []
        n: int = len(self)
        if maxsplit is None:
            maxsplit = -1

        if sep is None:
            i: int = n - 1
            while i >= 0:
                code: int = ord(self[i])
                if not (code == 32 or code == 9 or code == 10 or
                        code == 13 or code == 12 or code == 11):
                    break
                i = i - 1
            if i < 0:
                return result
            if maxsplit == 0:
                result.append(self[0:i + 1])
                return result

            splits: int = 0
            while i >= 0:
                if maxsplit >= 0 and splits >= maxsplit:
                    result.insert(0, self[0:i + 1])
                    return result
                end: int = i + 1
                while i >= 0:
                    code = ord(self[i])
                    if code == 32 or code == 9 or code == 10 or code == 13 or code == 12 or code == 11:
                        break
                    i = i - 1
                result.insert(0, self[i + 1:end])
                splits = splits + 1
                while i >= 0:
                    code = ord(self[i])
                    if not (code == 32 or code == 9 or code == 10 or
                            code == 13 or code == 12 or code == 11):
                        break
                    i = i - 1
            return result

        sep_len: int = len(sep)
        if sep_len == 0:
            raise ValueError("empty separator")
        end = n
        splits = 0
        i = n - sep_len
        while i >= 0:
            if (maxsplit < 0 or splits < maxsplit) and self[i:i + sep_len] == sep:
                result.insert(0, self[i + sep_len:end])
                end = i
                splits = splits + 1
                i = i - sep_len
            else:
                i = i - 1
        result.insert(0, self[0:end])
        return result

    def splitlines(self, keepends: bool = False) -> list:
        result: list = []
        n: int = len(self)
        start: int = 0
        i: int = 0
        if keepends is None:
            keepends = False
        while i < n:
            code: int = ord(self[i])
            if (code == 10 or code == 13 or code == 11 or code == 12 or
                    code == 28 or code == 29 or code == 30 or code == 133):
                break_end: int = i + 1
                if code == 13 and break_end < n and ord(self[break_end]) == 10:
                    break_end = break_end + 1
                if keepends:
                    result.append(self[start:break_end])
                else:
                    result.append(self[start:i])
                start = break_end
                i = break_end
            else:
                i = i + 1
        if start < n:
            result.append(self[start:n])
        return result

    def startswith(self, prefix: str, start: int = 0, end=None) -> bool:
        source_len: int = len(self)
        if start is None:
            start = 0
        if end is None:
            end = source_len
        if start < 0:
            start = start + source_len
            if start < 0:
                start = 0
        if end < 0:
            end = end + source_len
            if end < 0:
                end = 0
        if end > source_len:
            end = source_len
        prefix_len: int = len(prefix)
        if start > source_len or prefix_len > end - start:
            return False
        return self[start:start + prefix_len] == prefix

    def endswith(self, suffix: str, start: int = 0, end=None) -> bool:
        suffix_len: int = len(suffix)
        source_len: int = len(self)
        if start is None:
            start = 0
        if end is None:
            end = source_len
        if start < 0:
            start = start + source_len
            if start < 0:
                start = 0
        if end < 0:
            end = end + source_len
            if end < 0:
                end = 0
        if end > source_len:
            end = source_len
        if start > source_len or suffix_len > end - start:
            return False
        return self[end - suffix_len:end] == suffix

    def isdigit(self) -> bool:
        n: int = len(self)
        if n == 0:
            return False
        i: int = 0
        while i < n:
            code: int = ord(self[i])
            if code < 48 or code > 57:
                return False
            i = i + 1
        return True

    def isalpha(self) -> bool:
        n: int = len(self)
        if n == 0:
            return False
        i: int = 0
        while i < n:
            code: int = ord(self[i])
            if not ((code >= 65 and code <= 90) or
                    (code >= 97 and code <= 122)):
                return False
            i = i + 1
        return True

    def isalnum(self) -> bool:
        n: int = len(self)
        if n == 0:
            return False
        i: int = 0
        while i < n:
            code: int = ord(self[i])
            is_alpha: bool = ((code >= 65 and code <= 90) or
                              (code >= 97 and code <= 122))
            if not (is_alpha or (code >= 48 and code <= 57)):
                return False
            i = i + 1
        return True

    def isspace(self) -> bool:
        n: int = len(self)
        if n == 0:
            return False
        i: int = 0
        while i < n:
            code: int = ord(self[i])
            if not (code == 32 or code == 9 or code == 10 or code == 13 or
                    code == 12 or code == 11):
                return False
            i = i + 1
        return True

    def isupper(self) -> bool:
        has_alpha: bool = False
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if code >= 97 and code <= 122:
                return False
            if code >= 65 and code <= 90:
                has_alpha = True
            i = i + 1
        return has_alpha

    def islower(self) -> bool:
        has_alpha: bool = False
        i: int = 0
        n: int = len(self)
        while i < n:
            code: int = ord(self[i])
            if code >= 65 and code <= 90:
                return False
            if code >= 97 and code <= 122:
                has_alpha = True
            i = i + 1
        return has_alpha

    def casefold(self) -> str:
        return self.lower()

    def removeprefix(self, prefix: str) -> str:
        prefix_len: int = len(prefix)
        if self.startswith(prefix):
            return self[prefix_len:len(self)]
        return self[0:len(self)]

    def removesuffix(self, suffix: str) -> str:
        suffix_len: int = len(suffix)
        source_len: int = len(self)
        if suffix_len > 0 and self.endswith(suffix):
            return self[0:source_len - suffix_len]
        return self[0:source_len]

    def partition(self, sep: str) -> tuple:
        if len(sep) == 0:
            raise ValueError("empty separator")
        index: int = self.find(sep)
        if index < 0:
            return (self[0:len(self)], "", "")
        return (self[0:index], sep, self[index + len(sep):len(self)])

    def rpartition(self, sep: str) -> tuple:
        if len(sep) == 0:
            raise ValueError("empty separator")
        index: int = self.rfind(sep)
        if index < 0:
            return ("", "", self[0:len(self)])
        return (self[0:index], sep, self[index + len(sep):len(self)])

    def isascii(self) -> bool:
        i: int = 0
        while i < len(self):
            if ord(self[i]) > 127:
                return False
            i = i + 1
        return True

    def isdecimal(self) -> bool:
        return self.isdigit()

    def isnumeric(self) -> bool:
        return self.isdigit()

    def isidentifier(self) -> bool:
        n: int = len(self)
        if n == 0:
            return False
        code: int = ord(self[0])
        if not (self[0] == "_" or (code >= 65 and code <= 90) or
                (code >= 97 and code <= 122)):
            return False
        i: int = 1
        while i < n:
            code = ord(self[i])
            if not (self[i] == "_" or (code >= 65 and code <= 90) or
                    (code >= 97 and code <= 122) or
                    (code >= 48 and code <= 57)):
                return False
            i = i + 1
        return True

    def isprintable(self) -> bool:
        i: int = 0
        while i < len(self):
            code: int = ord(self[i])
            if code < 32 or code > 126:
                return False
            i = i + 1
        return True

    def istitle(self) -> bool:
        has_cased: bool = False
        previous_cased: bool = False
        i: int = 0
        while i < len(self):
            code: int = ord(self[i])
            if code >= 65 and code <= 90:
                if previous_cased:
                    return False
                has_cased = True
                previous_cased = True
            elif code >= 97 and code <= 122:
                if not previous_cased:
                    return False
                has_cased = True
                previous_cased = True
            else:
                previous_cased = False
            i = i + 1
        return has_cased

    def expandtabs(self, tabsize: int = 8) -> str:
        if tabsize is None:
            tabsize = 8
        if tabsize < 0:
            tabsize = 0
        result: str = ""
        column: int = 0
        i: int = 0
        while i < len(self):
            char: str = self[i]
            if char == "\t":
                spaces: int = 0
                if tabsize > 0:
                    spaces = tabsize - (column % tabsize)
                padding: str = ""
                count: int = 0
                while count < spaces:
                    padding = padding + " "
                    count = count + 1
                result = result + padding
                column = column + spaces
            else:
                result = result + char
                if char == "\n" or char == "\r":
                    column = 0
                else:
                    column = column + 1
            i = i + 1
        return result
