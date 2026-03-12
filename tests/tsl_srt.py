# tsl_srt.py - sorted() migrated from C to Python
# Regression test: sorted() is now PIR-backed via pyfncs.py

def main() -> None:
    # Basic sort
    items: list[int] = [3, 1, 4, 1, 5]
    result: list[int] = sorted(items)
    print(result)

    # Original unchanged
    print(items)

    # Already sorted
    print(sorted([1, 2, 3]))

    # Single element
    print(sorted([42]))

    # Empty list
    print(sorted([]))

    # Strings
    words: list[str] = ["banana", "apple", "cherry"]
    print(sorted(words))

    print("done")

main()
