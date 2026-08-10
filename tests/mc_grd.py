# Test guard conditions in match/case
def classify(x: int) -> str:
    match x:
        case n if n < 0:
            return "negative"
        case 0:
            return "zero"
        case n if n > 100:
            return "big"
        case n:
            return "small positive"

print(classify(-5))
print(classify(0))
print(classify(200))
print(classify(42))

# Guard with sequence pattern
def check_list(lst) -> None:
    match lst:
        case [x, y] if x == y:
            print("equal pair")
        case [x, y]:
            print("different pair")
        case _:
            print("not a pair")

check_list([3, 3])
check_list([1, 2])
check_list([1, 2, 3])
