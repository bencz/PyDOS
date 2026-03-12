def main() -> None:
    d: dict = {"x": 10, "y": 20, "z": 30}

    # Iterate over dict keys
    print("keys:")
    for k in d:
        print(k)

    # dict.keys()
    print("keys method:")
    keys: list = d.keys()
    for k in keys:
        print(k)

    # dict.values()
    print("values:")
    vals: list = d.values()
    for v in vals:
        print(v)

main()
