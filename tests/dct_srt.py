# Test: Dictionary sorting patterns
# Simulates sorted(dict.items()) using available features

def test_sort_by_key_asc() -> None:
    # Equivalent to: dict(sorted(meu_dict.items()))
    d: dict = {}
    d["c"] = 3
    d["a"] = 1
    d["b"] = 2

    # Get keys and sort ascending
    keys: list = []
    k: str = ""
    for k in d:
        keys.append(k)
    keys.sort()

    # Print in sorted key order
    i: int = 0
    while i < len(keys):
        print(keys[i], d[keys[i]])
        i = i + 1

def test_sort_by_key_desc() -> None:
    # Equivalent to: dict(sorted(meu_dict.items(), reverse=True))
    d: dict = {}
    d["c"] = 3
    d["a"] = 1
    d["b"] = 2

    # Get keys and sort descending
    keys: list = []
    k: str = ""
    for k in d:
        keys.append(k)
    keys.sort(True)

    # Print in reverse key order
    i: int = 0
    while i < len(keys):
        print(keys[i], d[keys[i]])
        i = i + 1

def test_sort_by_value_asc() -> None:
    # Equivalent to: sorted(meu_dict.items(), key=lambda item: item[1])
    # Manual approach: parallel lists for keys and values, sort by value
    d: dict = {}
    d["banana"] = 30
    d["apple"] = 10
    d["cherry"] = 20

    keys: list = []
    vals: list = []
    k: str = ""
    for k in d:
        keys.append(k)
        vals.append(d[k])

    # Insertion sort on vals, moving keys in parallel
    n: int = len(vals)
    i: int = 1
    while i < n:
        vkey: int = vals[i]
        kkey: str = keys[i]
        j: int = i - 1
        while j >= 0 and vals[j] > vkey:
            vals[j + 1] = vals[j]
            keys[j + 1] = keys[j]
            j = j - 1
        vals[j + 1] = vkey
        keys[j + 1] = kkey
        i = i + 1

    # Print sorted by value ascending
    i = 0
    while i < n:
        print(keys[i], vals[i])
        i = i + 1

def test_sort_by_value_desc() -> None:
    # Equivalent to: sorted(meu_dict.items(), key=lambda item: item[1], reverse=True)
    d: dict = {}
    d["banana"] = 30
    d["apple"] = 10
    d["cherry"] = 20

    keys: list = []
    vals: list = []
    k: str = ""
    for k in d:
        keys.append(k)
        vals.append(d[k])

    # Sort ascending then reverse both
    n: int = len(vals)
    i: int = 1
    while i < n:
        vkey: int = vals[i]
        kkey: str = keys[i]
        j: int = i - 1
        while j >= 0 and vals[j] > vkey:
            vals[j + 1] = vals[j]
            keys[j + 1] = keys[j]
            j = j - 1
        vals[j + 1] = vkey
        keys[j + 1] = kkey
        i = i + 1

    # Reverse for descending order
    vals.reverse()
    keys.reverse()

    # Print sorted by value descending
    i = 0
    while i < n:
        print(keys[i], vals[i])
        i = i + 1

print("=== Sort by key asc ===")
test_sort_by_key_asc()
print("=== Sort by key desc ===")
test_sort_by_key_desc()
print("=== Sort by value asc ===")
test_sort_by_value_asc()
print("=== Sort by value desc ===")
test_sort_by_value_desc()
