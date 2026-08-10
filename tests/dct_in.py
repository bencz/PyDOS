def main() -> None:
    d: dict = {"apple": 1, "banana": 2, "cherry": 3}

    # Test 'in' on dict keys
    if "apple" in d:
        print("apple found")
    if "banana" in d:
        print("banana found")
    if "grape" in d:
        print("grape found")
    else:
        print("grape not found")

    # Test 'not in' on dict
    if "cherry" not in d:
        print("cherry missing")
    else:
        print("cherry present")

    if "mango" not in d:
        print("mango missing")

main()
