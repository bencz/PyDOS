# lp_else.py - while/else and for/else tests

# Test 1: while/else — normal exit (else runs)
def test_while_else_normal() -> None:
    i: int = 0
    while i < 3:
        print(i)
        i = i + 1
    else:
        print("w1 else")

# Test 2: while/else — break (else skipped)
def test_while_else_break() -> None:
    i: int = 0
    while i < 10:
        if i == 2:
            break
        print(i)
        i = i + 1
    else:
        print("w2 else WRONG")
    print("w2 after")

# Test 3: while/else — condition false immediately (else runs)
def test_while_else_false() -> None:
    while False:
        print("w3 WRONG")
    else:
        print("w3 else")

# Test 4: for/else — normal exit (else runs)
def test_for_else_normal() -> None:
    for i in range(3):
        print(i)
    else:
        print("f1 else")

# Test 5: for/else — break (else skipped)
def test_for_else_break() -> None:
    for i in range(10):
        if i == 2:
            break
        print(i)
    else:
        print("f2 else WRONG")
    print("f2 after")

# Test 6: for/else — empty range (else runs)
def test_for_else_empty() -> None:
    for i in range(0):
        print("f3 WRONG")
    else:
        print("f3 else")

# Test 7: for/else over list — normal exit
def test_for_else_list() -> None:
    items: list[str] = ["a", "b", "c"]
    for s in items:
        print(s)
    else:
        print("f4 else")

# Test 8: for/else over list — break
def test_for_else_list_break() -> None:
    items: list[int] = [10, 20, 30, 40]
    for x in items:
        if x == 30:
            break
        print(x)
    else:
        print("f5 else WRONG")
    print("f5 after")

# Test 9: nested for/else — inner breaks, outer doesn't
def test_nested_for_else() -> None:
    for i in range(2):
        for j in range(5):
            if j == 1:
                break
            print(j)
        else:
            print("inner else WRONG")
        print(i)
    else:
        print("outer else")

# Test 10: while/else with search pattern
def test_search_pattern() -> None:
    nums: list[int] = [1, 3, 5, 7, 9]
    target: int = 5
    i: int = 0
    while i < len(nums):
        if nums[i] == target:
            print("found")
            break
        i = i + 1
    else:
        print("not found")

def main() -> None:
    test_while_else_normal()
    test_while_else_break()
    test_while_else_false()
    test_for_else_normal()
    test_for_else_break()
    test_for_else_empty()
    test_for_else_list()
    test_for_else_list_break()
    test_nested_for_else()
    test_search_pattern()
    print("all loop else tests done")

main()
