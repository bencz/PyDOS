import math
import operator


class AllOverloads:
    """
    Class implementing almost every overloadable operator
    supported by Python 3.12 for testing purposes.
    """

    def __init__(self, value):
        self.value = value

    # =========================
    # Representation
    # =========================
    def __repr__(self):
        return f"AllOverloads({self.value})"

    def __str__(self):
        return str(self.value)

    def __format__(self, format_spec):
        return format(self.value, format_spec)

    # =========================
    # Arithmetic Operators
    # =========================
    def __add__(self, other):
        return AllOverloads(self.value + self._get(other))

    def __sub__(self, other):
        return AllOverloads(self.value - self._get(other))

    def __mul__(self, other):
        return AllOverloads(self.value * self._get(other))

    def __truediv__(self, other):
        return AllOverloads(self.value / self._get(other))

    def __floordiv__(self, other):
        return AllOverloads(self.value // self._get(other))

    def __mod__(self, other):
        return AllOverloads(self.value % self._get(other))

    def __pow__(self, other, modulo=None):
        if modulo is not None:
            return AllOverloads(pow(self.value, self._get(other), modulo))
        return AllOverloads(self.value ** self._get(other))

    def __matmul__(self, other):
        return AllOverloads(self.value * self._get(other))

    # =========================
    # Reflected Operators
    # =========================
    def __radd__(self, other):
        return AllOverloads(self._get(other) + self.value)

    def __rsub__(self, other):
        return AllOverloads(self._get(other) - self.value)

    def __rmul__(self, other):
        return AllOverloads(self._get(other) * self.value)

    def __rtruediv__(self, other):
        return AllOverloads(self._get(other) / self.value)

    def __rfloordiv__(self, other):
        return AllOverloads(self._get(other) // self.value)

    def __rmod__(self, other):
        return AllOverloads(self._get(other) % self.value)

    def __rpow__(self, other):
        return AllOverloads(self._get(other) ** self.value)

    def __rmatmul__(self, other):
        return AllOverloads(self._get(other) * self.value)

    # =========================
    # In-place Operators
    # =========================
    def __iadd__(self, other):
        self.value += self._get(other)
        return self

    def __isub__(self, other):
        self.value -= self._get(other)
        return self

    def __imul__(self, other):
        self.value *= self._get(other)
        return self

    def __itruediv__(self, other):
        self.value /= self._get(other)
        return self

    def __ifloordiv__(self, other):
        self.value //= self._get(other)
        return self

    def __imod__(self, other):
        self.value %= self._get(other)
        return self

    def __ipow__(self, other):
        self.value **= self._get(other)
        return self

    def __imatmul__(self, other):
        self.value *= self._get(other)
        return self

    # =========================
    # Bitwise Operators
    # =========================
    def __and__(self, other):
        return AllOverloads(self.value & self._get(other))

    def __or__(self, other):
        return AllOverloads(self.value | self._get(other))

    def __xor__(self, other):
        return AllOverloads(self.value ^ self._get(other))

    def __lshift__(self, other):
        return AllOverloads(self.value << self._get(other))

    def __rshift__(self, other):
        return AllOverloads(self.value >> self._get(other))

    def __invert__(self):
        return AllOverloads(~self.value)

    # =========================
    # Comparisons
    # =========================
    def __eq__(self, other):
        return self.value == self._get(other)

    def __ne__(self, other):
        return self.value != self._get(other)

    def __lt__(self, other):
        return self.value < self._get(other)

    def __le__(self, other):
        return self.value <= self._get(other)

    def __gt__(self, other):
        return self.value > self._get(other)

    def __ge__(self, other):
        return self.value >= self._get(other)

    # =========================
    # Unary Operators
    # =========================
    def __neg__(self):
        return AllOverloads(-self.value)

    def __pos__(self):
        return AllOverloads(+self.value)

    def __abs__(self):
        return AllOverloads(abs(self.value))

    # =========================
    # Numeric Conversions
    # =========================
    def __int__(self):
        return int(self.value)

    def __float__(self):
        return float(self.value)

    def __complex__(self):
        return complex(self.value)

    def __bool__(self):
        return bool(self.value)

    def __round__(self, ndigits=None):
        return AllOverloads(round(self.value, ndigits) if ndigits else round(self.value))

    def __floor__(self):
        return math.floor(self.value)

    def __ceil__(self):
        return math.ceil(self.value)

    def __trunc__(self):
        return math.trunc(self.value)

    # =========================
    # Container Protocol
    # =========================
    def __len__(self):
        return abs(int(self.value))

    def __getitem__(self, item):
        return self.value

    def __setitem__(self, key, value):
        self.value = value

    def __delitem__(self, key):
        self.value = 0

    def __contains__(self, item):
        return item == self.value

    def __iter__(self):
        yield self.value

    # =========================
    # Callable
    # =========================
    def __call__(self, x):
        return AllOverloads(self.value + x)

    # =========================
    # Context Manager
    # =========================
    def __enter__(self):
        print("Entering context")
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        print("Exiting context")
        return False

    # =========================
    # Hash
    # =========================
    def __hash__(self):
        return hash(self.value)

    # =========================
    # Helper
    # =========================
    def _get(self, other):
        return other.value if isinstance(other, AllOverloads) else other


# ==========================================================
# Test Layer
# ==========================================================
def main():
    print("=== Arithmetic ===")
    a = AllOverloads(10)
    b = AllOverloads(3)

    print(a + b)
    print(a - b)
    print(a * b)
    print(a / b)
    print(a // b)
    print(a % b)
    print(a ** b)
    print(a @ b)

    print("=== Reflected ===")
    print(5 + a)
    print(5 - a)
    print(5 * a)
    print(5 / a)

    print("=== In-place ===")
    a += b
    print(a)
    a -= b
    print(a)

    print("=== Bitwise ===")
    print(a & b)
    print(a | b)
    print(a ^ b)
    print(a << 1)
    print(a >> 1)
    print(~a)

    print("=== Comparison ===")
    print(a == b)
    print(a != b)
    print(a < b)
    print(a <= b)
    print(a > b)
    print(a >= b)

    print("=== Unary ===")
    print(-a)
    print(+a)
    print(abs(a))

    print("=== Conversion ===")
    print(int(a))
    print(float(a))
    print(bool(a))
    print(round(a))

    print("=== Container ===")
    print(len(a))
    print(a[0])
    a[0] = 99
    print(a)
    del a[0]
    print(a)
    print(99 in a)

    print("=== Callable ===")
    print(a(5))

    print("=== Context Manager ===")
    with AllOverloads(42) as ctx:
        print(ctx)

    print("=== Hash ===")
    print(hash(a))