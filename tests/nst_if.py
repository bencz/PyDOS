def classify(n: int) -> str:
    if n < 0:
        return "negative"
    elif n == 0:
        return "zero"
    elif n < 10:
        return "small"
    elif n < 100:
        return "medium"
    else:
        return "large"

print(classify(-5))
print(classify(0))
print(classify(7))
print(classify(42))
print(classify(999))
