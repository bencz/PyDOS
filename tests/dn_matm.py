# dn_matm.py - Matmul operator tests (IR_MATMUL)
# Tests: __matmul__ via @ operator

class Matrix:
    def __init__(self, a: int, b: int, c: int, d: int) -> None:
        self.a = a
        self.b = b
        self.c = c
        self.d = d

    def __matmul__(self, other: Matrix) -> Matrix:
        return Matrix(
            self.a * other.a + self.b * other.c,
            self.a * other.b + self.b * other.d,
            self.c * other.a + self.d * other.c,
            self.c * other.b + self.d * other.d
        )

    def __str__(self) -> str:
        return "[" + str(self.a) + " " + str(self.b) + "; " + str(self.c) + " " + str(self.d) + "]"

# Identity matrix
I: Matrix = Matrix(1, 0, 0, 1)

# Test matrix
A: Matrix = Matrix(2, 3, 4, 5)

# A @ I should equal A
r1: Matrix = A @ I
print(r1)

# I @ A should equal A
r2: Matrix = I @ A
print(r2)

# A @ A
r3: Matrix = A @ A
print(r3)

# Chain: (A @ I) @ A
r4: Matrix = (A @ I) @ A
print(r4)
