class Complex:
    real: int
    imag: int

    def __init__(self, real: int, imag: int) -> None:
        self.real = real
        self.imag = imag

    def __add__(self, other: Complex) -> Complex:
        return Complex(self.real + other.real, self.imag + other.imag)

    def __str__(self) -> str:
        return str(self.real) + "+" + str(self.imag) + "i"

a: Complex = Complex(3, 4)
b: Complex = Complex(1, 2)
c: Complex = a + b
print(c)
print(str(a) + " + " + str(b) + " = " + str(c))
