# Test: list.sort() method
# Tests in-place sorting, reverse parameter, and dict+list combinations

def test_sort_basic() -> None:
    # Sort integers
    nums: list = [3, 1, 4, 1, 5, 9, 2, 6]
    nums.sort()
    print(nums)

    # Sort already sorted
    a: list = [1, 2, 3]
    a.sort()
    print(a)

    # Sort descending order
    b: list = [5, 4, 3, 2, 1]
    b.sort()
    print(b)

    # Sort strings
    words: list = ["banana", "apple", "cherry"]
    words.sort()
    print(words)

    # Sort single element
    c: list = [42]
    c.sort()
    print(c)

    # Sort empty
    d: list = []
    d.sort()
    print(d)

    # Sort negative numbers
    e: list = [-3, 0, -1, 5, -2]
    e.sort()
    print(e)

def test_sort_reverse() -> None:
    # Sort integers in reverse (descending)
    nums: list = [3, 1, 4, 1, 5, 9, 2, 6]
    nums.sort(True)
    print(nums)

    # Sort strings in reverse
    words: list = ["banana", "apple", "cherry"]
    words.sort(True)
    print(words)

    # Sort reverse with negative numbers
    neg: list = [-3, 0, -1, 5, -2]
    neg.sort(True)
    print(neg)

    # Sort reverse=False (same as no arg)
    f: list = [5, 2, 8, 1]
    f.sort(False)
    print(f)

def test_reverse() -> None:
    # list.reverse() method
    a: list = [1, 2, 3, 4, 5]
    a.reverse()
    print(a)

    # Reverse strings
    b: list = ["x", "y", "z"]
    b.reverse()
    print(b)

    # Reverse single
    c: list = [99]
    c.reverse()
    print(c)

def test_dict_with_lists() -> None:
    # Dictionary with list values — sort the list values
    fruits: list = ["banana", "apple", "cherry"]
    nums: list = [30, 10, 20]

    d: dict = {}
    d["fruits"] = fruits
    d["nums"] = nums

    # Sort the lists stored in the dict
    fruits.sort()
    print(d["fruits"])

    nums.sort()
    print(d["nums"])

    # Reverse the sorted lists
    fruits.reverse()
    print(d["fruits"])

    nums.reverse()
    print(d["nums"])

    # Sort descending via sort(True)
    fruits.sort(True)
    print(d["fruits"])

    nums.sort(True)
    print(d["nums"])

test_sort_basic()
test_sort_reverse()
test_reverse()
test_dict_with_lists()
