class Matrix:
    """Represents a square N x N matrix."""

    def __init__(self, data):
        if isinstance(data, Matrix):
            data = data.to_list()

        if not isinstance(data, list) or not data:
            raise ValueError("Matrix data must be a non-empty list.")

        size = len(data)

        for row in data:
            if not isinstance(row, list) or len(row) != size:
                raise ValueError("The matrix must be square, with shape N x N.")

        self._data = [row[:] for row in data]
        self._size = size

    @property
    def size(self):
        """Returns the matrix order N."""
        return self._size

    @property
    def shape(self):
        """Returns the matrix shape."""
        return self._size, self._size

    @property
    def T(self):
        """Returns the transposed matrix."""
        return self.transpose()

    @classmethod
    def zeros(cls, size):
        """Creates an N x N matrix filled with zeros."""
        cls._validate_size(size)

        return cls([
            [0 for _ in range(size)]
            for _ in range(size)
        ])

    @classmethod
    def identity(cls, size):
        """Creates an N x N identity matrix."""
        cls._validate_size(size)

        return cls([
            [
                1 if row == column else 0
                for column in range(size)
            ]
            for row in range(size)
        ])

    @staticmethod
    def _validate_size(size):
        if not isinstance(size, int) or isinstance(size, bool) or size <= 0:
            raise ValueError("Matrix size must be a positive integer.")

    def _validate_same_size(self, other):
        if not isinstance(other, Matrix):
            raise TypeError("The operation requires another Matrix.")

        if self._size != other._size:
            raise ValueError("Matrices must have the same size.")

    def copy(self):
        """Returns an independent copy of the matrix."""
        return Matrix(self._data)

    def to_list(self):
        """Returns the matrix as a list of lists."""
        return [row[:] for row in self._data]

    def transpose(self):
        """Returns the transposed matrix."""
        return Matrix([
            [
                self[column, row]
                for column in range(self._size)
            ]
            for row in range(self._size)
        ])

    def trace(self):
        """Returns the sum of the main diagonal."""
        return sum(
            self[index, index]
            for index in range(self._size)
        )

    def determinant(self, tolerance=1e-12):
        """Calculates the determinant using Gaussian elimination."""
        data = self.to_list()
        result = 1

        for column in range(self._size):
            pivot_row = max(
                range(column, self._size),
                key=lambda row: abs(data[row][column])
            )

            if abs(data[pivot_row][column]) < tolerance:
                return 0

            if pivot_row != column:
                data[column], data[pivot_row] = (
                    data[pivot_row],
                    data[column]
                )
                result *= -1

            pivot = data[column][column]
            result *= pivot

            for row in range(column + 1, self._size):
                factor = data[row][column] / pivot

                data[row][column] = 0

                for current_column in range(
                        column + 1,
                        self._size
                ):
                    data[row][current_column] -= (
                            factor * data[column][current_column]
                    )

        if isinstance(result, float):
            nearest_integer = round(result)

            if abs(result - nearest_integer) < tolerance:
                return nearest_integer

        return result

    def inverse(self, tolerance=1e-12):
        """Returns the inverse using Gauss-Jordan elimination."""
        size = self._size
        identity_data = Matrix.identity(size).to_list()

        augmented = [
            self._data[row][:] + identity_data[row]
            for row in range(size)
        ]

        for column in range(size):
            pivot_row = max(
                range(column, size),
                key=lambda row: abs(augmented[row][column])
            )

            if abs(augmented[pivot_row][column]) < tolerance:
                raise ValueError(
                    "The matrix is singular and has no inverse."
                )

            if pivot_row != column:
                augmented[column], augmented[pivot_row] = (
                    augmented[pivot_row],
                    augmented[column]
                )

            pivot = augmented[column][column]

            for current_column in range(2 * size):
                augmented[column][current_column] /= pivot

            for row in range(size):
                if row == column:
                    continue

                factor = augmented[row][column]

                for current_column in range(2 * size):
                    augmented[row][current_column] -= (
                            factor
                            * augmented[column][current_column]
                    )

        return Matrix([
            row[size:]
            for row in augmented
        ])

    def almost_equals(self, other, tolerance=1e-9):
        """Compares two matrices using a numerical tolerance."""
        if not isinstance(other, Matrix):
            return False

        if self._size != other._size:
            return False

        for row in range(self._size):
            for column in range(self._size):
                difference = (
                        self[row, column]
                        - other[row, column]
                )

                if abs(difference) > tolerance:
                    return False

        return True

    def __len__(self):
        return self._size

    def __getitem__(self, index):
        if isinstance(index, tuple):
            if len(index) != 2:
                raise IndexError(
                    "Use matrix[row, column] to access an element."
                )

            row, column = index
            return self._data[row][column]

        return tuple(self._data[index])

    def __setitem__(self, index, value):
        if not isinstance(index, tuple) or len(index) != 2:
            raise IndexError(
                "Use matrix[row, column] = value."
            )

        row, column = index
        self._data[row][column] = value

    def __iter__(self):
        for row in self._data:
            yield tuple(row)

    def __repr__(self):
        return f"Matrix({self._data!r})"

    def __str__(self):
        return "\n".join(
            "[ " + "  ".join(str(value) for value in row) + " ]"
            for row in self._data
        )

    def __eq__(self, other):
        if not isinstance(other, Matrix):
            return False

        return self._data == other._data

    def __add__(self, other):
        if not isinstance(other, Matrix):
            return NotImplemented

        self._validate_same_size(other)

        return Matrix([
            [
                self[row, column] + other[row, column]
                for column in range(self._size)
            ]
            for row in range(self._size)
        ])

    def __sub__(self, other):
        if not isinstance(other, Matrix):
            return NotImplemented

        self._validate_same_size(other)

        return Matrix([
            [
                self[row, column] - other[row, column]
                for column in range(self._size)
            ]
            for row in range(self._size)
        ])

    def __neg__(self):
        return Matrix([
            [-value for value in row]
            for row in self._data
        ])

    def __mul__(self, other):
        if isinstance(other, Matrix):
            return self @ other

        if not isinstance(other, (int, float, complex)):
            return NotImplemented

        return Matrix([
            [value * other for value in row]
            for row in self._data
        ])

    def __rmul__(self, other):
        if not isinstance(other, (int, float, complex)):
            return NotImplemented

        return self * other

    def __truediv__(self, scalar):
        if not isinstance(scalar, (int, float, complex)):
            return NotImplemented

        if scalar == 0:
            raise ZeroDivisionError(
                "A matrix cannot be divided by zero."
            )

        return Matrix([
            [value / scalar for value in row]
            for row in self._data
        ])

    def __matmul__(self, other):
        if not isinstance(other, Matrix):
            return NotImplemented

        self._validate_same_size(other)

        result = Matrix.zeros(self._size)

        for row in range(self._size):
            for column in range(self._size):
                total = 0

                for index in range(self._size):
                    total += (
                            self[row, index]
                            * other[index, column]
                    )

                result[row, column] = total

        return result

    def __pow__(self, exponent):
        if not isinstance(exponent, int) or isinstance(exponent, bool):
            raise TypeError(
                "The matrix exponent must be an integer."
            )

        if exponent == 0:
            return Matrix.identity(self._size)

        if exponent < 0:
            return self.inverse() ** (-exponent)

        result = Matrix.identity(self._size)
        base = self.copy()

        while exponent > 0:
            if exponent % 2 == 1:
                result = result @ base

            base = base @ base
            exponent //= 2

        return result

matrix_a = Matrix([
    [1, 2],
    [3, 4]
])

matrix_b = Matrix([
    [5, 6],
    [7, 8]
])

print("Matrix A:")
print(matrix_a)

print("\nAddition:")
print(matrix_a + matrix_b)

print("\nSubtraction:")
print(matrix_a - matrix_b)

print("\nNegation:")
print(-matrix_a)

print("\nScalar multiplication:")
print(matrix_a * 3)
print(3 * matrix_a)

print("\nScalar division:")
print(matrix_a / 2)

print("\nMatrix multiplication:")
print(matrix_a @ matrix_b)

print("\nMatrix multiplication using *:")
print(matrix_a * matrix_b)

print("\nMatrix power:")
print(matrix_a ** 2)

print("\nTranspose:")
print(matrix_a.T)

print("\nTrace:")
print(matrix_a.trace())

print("\nDeterminant:")
print(matrix_a.determinant())

print("\nInverse:")
print(matrix_a.inverse())

print("\nA multiplied by its inverse:")
print(matrix_a @ matrix_a.inverse())

print("\nElement at row 0, column 1:")
print(matrix_a[0, 1])

matrix_a[0, 1] = 10

print("\nModified matrix:")
print(matrix_a)