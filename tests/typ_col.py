class TypedList[T]:

    def __init__(self) -> None:
        self._data: list[T] = []

    def add(self, item: T) -> None:
        self._data.append(item)

    def get(self, index: int, default: T | None = None) -> T | None:
        if 0 <= index < len(self._data):
            return self._data[index]
        return default

    def has(self, item: T) -> bool:
        return item in self._data

    def remove(self, item: T) -> bool:
        if item in self._data:
            self._data.remove(item)
            return True
        return False

    def remove_at(self, index: int) -> T | None:
        if 0 <= index < len(self._data):
            return self._data.pop(index)
        return None

    def index_of(self, item: T) -> int:
        if item in self._data:
            return self._data.index(item)
        return -1

    def size(self) -> int:
        return len(self._data)

    def clear(self) -> None:
        self._data.clear()

    def items(self) -> list[T]:
        return list(self._data)

    def __iter__(self):
        return iter(self._data)

    def __repr__(self) -> str:
        return f"TypedList({self._data})"


class TypedDict[K, V]:

    def __init__(self) -> None:
        self._data: dict[K, V] = {}

    def set(self, key: K, value: V) -> None:
        self._data[key] = value

    def get(self, key: K, default: V | None = None) -> V | None:
        return self._data.get(key, default)

    def has(self, key: K) -> bool:
        return key in self._data

    def remove(self, key: K) -> bool:
        if key in self._data:
            del self._data[key]
            return True
        return False

    def keys(self) -> list[K]:
        return list(self._data.keys())

    def values(self) -> list[V]:
        return list(self._data.values())

    def items(self) -> list[tuple[K, V]]:
        return list(self._data.items())

    def size(self) -> int:
        return len(self._data)

    def clear(self) -> None:
        self._data.clear()

    def __repr__(self) -> str:
        return f"TypedDict({self._data})"


# --- usage ---

data: TypedDict[str, TypedDict[int, TypedList[str]]] = TypedDict()

# fruits
fruits: TypedDict[int, TypedList[str]] = TypedDict()

tropical: TypedList[str] = TypedList()
tropical.add("mango")
tropical.add("papaya")
tropical.add("pineapple")

citrus: TypedList[str] = TypedList()
citrus.add("orange")
citrus.add("lemon")

fruits.set(1, tropical)
fruits.set(2, citrus)
data.set("fruits", fruits)

# colors
colors: TypedDict[int, TypedList[str]] = TypedDict()

warm: TypedList[str] = TypedList()
warm.add("red")
warm.add("orange")
warm.add("yellow")

cool: TypedList[str] = TypedList()
cool.add("blue")
cool.add("green")

colors.set(10, warm)
colors.set(20, cool)
data.set("colors", colors)

# get
print(data.get("fruits").get(1))          # TypedList(['mango', 'papaya', 'pineapple'])
print(data.get("fruits").get(1).get(0))   # mango

# check existence
print(data.has("fruits"))                  # True
print(data.get("fruits").get(1).has("papaya"))  # True

# remove item from list
tropical.remove("papaya")
print(tropical)                            # TypedList(['mango', 'pineapple'])

# add new category
animals: TypedDict[int, TypedList[str]] = TypedDict()
pets: TypedList[str] = TypedList()
pets.add("dog")
pets.add("cat")
animals.set(1, pets)
data.set("animals", animals)

# iterate everything
for outer_key, inner_dict in data.items():
    for inner_key, typed_list in inner_dict.items():
        for item in typed_list:
            print(f"{outer_key} -> {inner_key} -> {item}")
