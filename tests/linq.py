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


class Queryable[T]:

    def __init__(self, source) -> None:
        if isinstance(source, TypedList):
            self._source = source.items()
        elif isinstance(source, list):
            self._source = source
        else:
            # generator or any iterable
            self._source = source
        self._materialized: list[T] | None = None

    def _materialize(self) -> list[T]:
        if self._materialized is None:
            if isinstance(self._source, list):
                self._materialized = self._source
            else:
                self._materialized = list(self._source)
        return self._materialized

    def _lazy_where(self, source, predicate):
        for item in source:
            if predicate(item):
                yield item

    def _lazy_select(self, source, selector):
        for item in source:
            yield selector(item)

    def _lazy_select_many(self, source, selector):
        for item in source:
            inner = selector(item)
            if isinstance(inner, TypedList):
                for sub in inner:
                    yield sub
            else:
                for sub in inner:
                    yield sub

    def _lazy_skip(self, source, count):
        i: int = 0
        for item in source:
            if i >= count:
                yield item
            i += 1

    def _lazy_take(self, source, count):
        i: int = 0
        for item in source:
            if i >= count:
                break
            yield item
            i += 1

    def _lazy_skip_while(self, source, predicate):
        skipping: bool = True
        for item in source:
            if skipping and predicate(item):
                continue
            skipping = False
            yield item

    def _lazy_take_while(self, source, predicate):
        for item in source:
            if not predicate(item):
                break
            yield item

    def _lazy_distinct(self, source):
        seen: list = []
        for item in source:
            if item not in seen:
                seen.append(item)
                yield item

    def _lazy_concat(self, source_a, source_b):
        for item in source_a:
            yield item
        for item in source_b:
            yield item

    def _lazy_except_of(self, source, other_list):
        for item in source:
            if item not in other_list:
                yield item

    def _lazy_intersect(self, source, other_list):
        seen: list = []
        for item in source:
            if item in other_list and item not in seen:
                seen.append(item)
                yield item

    # --- lazy chainable operations ---

    def where(self, predicate) -> "Queryable":
        return Queryable(self._lazy_where(self._source, predicate))

    def select(self, selector) -> "Queryable":
        return Queryable(self._lazy_select(self._source, selector))

    def select_many(self, selector) -> "Queryable":
        return Queryable(self._lazy_select_many(self._source, selector))

    def skip(self, count: int) -> "Queryable":
        return Queryable(self._lazy_skip(self._source, count))

    def take(self, count: int) -> "Queryable":
        return Queryable(self._lazy_take(self._source, count))

    def skip_while(self, predicate) -> "Queryable":
        return Queryable(self._lazy_skip_while(self._source, predicate))

    def take_while(self, predicate) -> "Queryable":
        return Queryable(self._lazy_take_while(self._source, predicate))

    def distinct(self) -> "Queryable":
        return Queryable(self._lazy_distinct(self._source))

    def concat(self, other: "Queryable") -> "Queryable":
        return Queryable(self._lazy_concat(self._source, other._source))

    def except_of(self, other: "Queryable") -> "Queryable":
        other_list = other._materialize()
        return Queryable(self._lazy_except_of(self._source, other_list))

    def intersect(self, other: "Queryable") -> "Queryable":
        other_list = other._materialize()
        return Queryable(self._lazy_intersect(self._source, other_list))

    # --- eager operations (need sorting) ---

    def order_by(self, key) -> "Queryable":
        return Queryable(sorted(self._materialize(), key=key))

    def order_by_descending(self, key) -> "Queryable":
        return Queryable(sorted(self._materialize(), key=key, reverse=True))

    # --- grouping ---

    def group_by(self, key_selector) -> TypedDict:
        groups: TypedDict = TypedDict()
        for item in self._source:
            k = key_selector(item)
            if not groups.has(k):
                groups.set(k, TypedList())
            groups.get(k).add(item)
        return groups

    # --- join ---

    def join(self, inner: "Queryable", outer_key, inner_key, result_selector) -> "Queryable":
        inner_list = inner._materialize()
        results: list = []
        for outer_item in self._source:
            for inner_item in inner_list:
                if outer_key(outer_item) == inner_key(inner_item):
                    results.append(result_selector(outer_item, inner_item))
        return Queryable(results)

    # --- quantifiers ---

    def any(self, predicate=None) -> bool:
        for item in self._source:
            if predicate is None or predicate(item):
                return True
        return False

    def all(self, predicate) -> bool:
        for item in self._source:
            if not predicate(item):
                return False
        return True

    def contains(self, value) -> bool:
        for item in self._source:
            if item == value:
                return True
        return False

    # --- aggregation ---

    def count(self, predicate=None) -> int:
        total: int = 0
        for item in self._materialize():
            if predicate is None or predicate(item):
                total += 1
        return total

    def sum(self, selector=None) -> int | float:
        total: int | float = 0
        for item in self._materialize():
            if selector is not None:
                total += selector(item)
            else:
                total += item
        return total

    def min(self, selector=None):
        data = self._materialize()
        if len(data) == 0:
            return None
        best = data[0]
        best_val = selector(best) if selector is not None else best
        i: int = 1
        while i < len(data):
            item = data[i]
            val = selector(item) if selector is not None else item
            if val < best_val:
                best_val = val
                best = item
            i += 1
        return best_val if selector is not None else best

    def max(self, selector=None):
        data = self._materialize()
        if len(data) == 0:
            return None
        best = data[0]
        best_val = selector(best) if selector is not None else best
        i: int = 1
        while i < len(data):
            item = data[i]
            val = selector(item) if selector is not None else item
            if val > best_val:
                best_val = val
                best = item
            i += 1
        return best_val if selector is not None else best

    def average(self, selector=None) -> float | None:
        data = self._materialize()
        if len(data) == 0:
            return None
        total: int | float = 0
        for item in data:
            if selector is not None:
                total += selector(item)
            else:
                total += item
        return total / len(data)

    # --- element access ---

    def first(self, predicate=None):
        for item in self._source:
            if predicate is None or predicate(item):
                return item
        return None

    def last(self, predicate=None):
        result = None
        for item in self._materialize():
            if predicate is None or predicate(item):
                result = item
        return result

    def single(self, predicate=None):
        found = None
        count: int = 0
        for item in self._materialize():
            if predicate is None or predicate(item):
                found = item
                count += 1
                if count > 1:
                    raise ValueError("sequence contains more than one matching element")
        return found

    def element_at(self, index: int):
        data = self._materialize()
        if 0 <= index < len(data):
            return data[index]
        return None

    # --- conversion ---

    def to_list(self) -> TypedList:
        result: TypedList = TypedList()
        for item in self._materialize():
            result.add(item)
        return result

    def to_dict(self, key_selector, value_selector=None) -> TypedDict:
        result: TypedDict = TypedDict()
        for item in self._materialize():
            k = key_selector(item)
            v = value_selector(item) if value_selector is not None else item
            result.set(k, v)
        return result

    # --- side effect ---

    def for_each(self, action) -> None:
        for item in self._materialize():
            action(item)

    def __iter__(self):
        return iter(self._materialize())

    def __repr__(self) -> str:
        return f"Queryable({self._materialize()})"


def query[T](source: TypedList[T] | list[T]) -> Queryable[T]:
    return Queryable(source)


# =============================================
# usage
# =============================================

class Person:
    def __init__(self, name: str, age: int, city: str) -> None:
        self.name = name
        self.age = age
        self.city = city
    def __repr__(self) -> str:
        return f"{self.name}({self.age}, {self.city})"


people: TypedList[Person] = TypedList()
people.add(Person("Alice", 30, "NYC"))
people.add(Person("Bob", 25, "LA"))
people.add(Person("Charlie", 35, "NYC"))
people.add(Person("Diana", 28, "LA"))
people.add(Person("Eve", 30, "Chicago"))

# lazy chain: nothing executes until to_list()
result = (
    query(people)
    .where(lambda p: p.age >= 28)
    .order_by_descending(lambda p: p.age)
    .select(lambda p: f"{p.name} - {p.age}")
    .take(3)
    .to_list()
)
print("Lazy chain:", result)

# first with lazy (stops at first match, no full scan)
oldest_nyc = query(people).where(lambda p: p.city == "NYC").order_by_descending(lambda p: p.age).first()
print("First NYC by age desc:", oldest_nyc)

# group_by
groups = query(people).group_by(lambda p: p.city)
for city, group in groups.items():
    print(f"City {city}:", group)

# aggregation (materializes only when needed)
print("Avg age >= 28:", query(people).where(lambda p: p.age >= 28).average(lambda p: p.age))

# any (short-circuits on first match)
print("Any Chicago:", query(people).any(lambda p: p.city == "Chicago"))

# distinct lazy
numbers: TypedList[int] = TypedList()
for n in [1, 2, 2, 3, 3, 3, 4]:
    numbers.add(n)
print("Distinct:", query(numbers).distinct().to_list())

# join lazy
cities: TypedList[dict] = TypedList()
cities.add({"name": "NYC", "state": "New York"})
cities.add({"name": "LA", "state": "California"})
cities.add({"name": "Chicago", "state": "Illinois"})

joined = (
    query(people)
    .join(
        query(cities),
        lambda p: p.city,
        lambda c: c["name"],
        lambda p, c: f"{p.name} lives in {c['name']}, {c['state']}"
    )
    .to_list()
)
print("Joined:", joined)