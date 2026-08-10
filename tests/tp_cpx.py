# tp_cpx.py - complex type integration test

# Complex literal
c = 3j
print(c)

# Complex arithmetic via literal syntax
c2 = 1 + 2j
print(c2)

# Addition
c3 = (1+2j) + (3+4j)
print(c3)

# Subtraction
c4 = (5+7j) - (2+3j)
print(c4)

# Multiplication: (1+2j)*(3+4j) = -5+10j
c5 = (1+2j) * (3+4j)
print(c5)

# Negation
c6 = -(3+4j)
print(c6)

# Bool: non-zero complex is truthy
if 1j:
    print("truthy")

# Bool: zero complex is falsy
if not 0j:
    print("falsy")

# complex() constructor
c7 = complex(3, 4)
print(c7)

# .real and .imag
c8 = 3+4j
print(c8.real)
print(c8.imag)

# conjugate
c9 = (3+4j).conjugate()
print(c9)

print("done")
