def test_str_slice() -> None:
    s: str = "Hello, World!"

    # Basic slicing
    print(s[0:5])
    print(s[7:12])

    # Slice with step
    print(s[0:5:2])

    # Slice to end
    print(s[7:2147483647])

def test_list_slice() -> None:
    nums: list = [10, 20, 30, 40, 50]

    # Basic slicing
    sub: list = nums[1:4]
    print(sub)

    # Slice from start
    sub = nums[0:3]
    print(sub)

    # Slice with step
    sub = nums[0:5:2]
    print(sub)

def main() -> None:
    test_str_slice()
    test_list_slice()

main()
