data: dict[str, dict[int, list[str]]] = {}

data["fruits"] = {}
data["fruits"][1] = ["apple", "banana"]
data["fruits"][2] = ["grape"]

data["colors"] = {}
data["colors"][10] = ["blue", "green", "red"]

# get with default value
value: list[str] = data.get("fruits", {}).get(1, [])
print(value)

# get on nonexistent key
value = data.get("animals", {}).get(5, [])
print(value)

# check existence
if "fruits" in data:
    if 2 in data["fruits"]:
        print(data["fruits"][2])

# append item to existing list
data["fruits"][1].append("orange")

# remove a key
del data["colors"][10]

# iterate
for outer_key, inner_dict in data.items():
    for inner_key, str_list in inner_dict.items():
        print(f"{outer_key} -> {inner_key} -> {str_list}")
