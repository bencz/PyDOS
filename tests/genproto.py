"""Generic class with the full protocol surface around it.

Several defects met in this one class.  A monomorphized class was emitted
ahead of the descriptor classes its decorators need, so any decorated method
in a generic class applied staticmethod or property before those classes
existed.  Calling a variable that holds an instance was bound against
__init__ instead of __call__, so the constructor defaults were injected as
arguments.  classmethod and staticmethod never received their defaults.  A
generic type alias dropped its parameters, and "for i in range(n)" inside a
generator lost the loop bounds at the first yield.
"""

type Pair[T] = tuple[T, T]
type Record[T] = tuple[str, T]
type Result[T] = tuple[bool, T | None, str]


class Box[T]:
    __match_args__ = ("value", "label")

    instances_created = 0
    category = "Box"

    def __init__(self, value: T, label: str = "unlabeled") -> None:
        self.__value = value
        self._label = label
        type(self).instances_created += 1

    @property
    def value(self) -> T:
        return self.__value

    @value.setter
    def value(self, new_value: T) -> None:
        self.__value = new_value

    @property
    def label(self) -> str:
        return self._label

    @staticmethod
    def values_are_equal(left: object, right: object) -> bool:
        return left == right

    @classmethod
    def empty(cls, label: str = "empty"):
        return cls(None, label)

    def filter(self, predicate) -> Result[T]:
        accepted = bool(predicate(self.__value))
        if accepted:
            return (True, self.__value, "value accepted")
        return (False, None, "value rejected")

    def generate(self, repetitions: int = 1):
        for index in range(repetitions):
            yield index, self.__value

    def __iter__(self):
        yield self._label
        yield self.__value

    def __len__(self) -> int:
        return len(self.__value)

    def __contains__(self, item: object) -> bool:
        return item in self.__value

    def __getitem__(self, index):
        return self.__value[index]

    def __call__(self, function):
        return function(self.__value)

    def __repr__(self) -> str:
        return "Box(value=" + repr(self.__value) + ", label=" + repr(self._label) + ")"


box = Box((2, 3, 5, 7), "primes")
print(box)
print("unpacked:", tuple(box))
print("length:", len(box), "contains 5:", 5 in box, "index 2:", box[2])
print("callable:", box(lambda numbers: sum(numbers)))
print("filter:", box.filter(lambda numbers: all(n > 0 for n in numbers)))
print("rejected:", box.filter(lambda numbers: False))
print("generator:", tuple(box.generate(3)))
print("generator empty:", tuple(box.generate(0)))
print("staticmethod:", Box.values_are_equal((1, 2), (1, 2)))
print("staticmethod false:", Box.values_are_equal((1, 2), (1, 3)))
print("classmethod:", Box.empty())
print("classmethod arg:", Box.empty("named"))
print("class attribute:", Box.instances_created, Box.category)

box.value = (11, 13)
print("property setter:", box.value, box.label)

text_box = Box("hello", "text")
print("text:", len(text_box), "e" in text_box, text_box[1])
print("properties:", text_box.value, text_box.label)

pair: Pair[int] = (10, 20)
record: Record[int] = ("age", 17)
result: Result[str] = (True, "value", "ok")
print("aliases:", pair, record, result)


def first[T](values: tuple) -> T:
    return values[0]


def swap[T](pair_value: Pair[T]) -> Pair[T]:
    left, right = pair_value
    return right, left


print("generic function:", first(("a", "b", "c")), swap(pair))


class Registry[K, V]:
    def __init__(self) -> None:
        self.entries = {}

    def put(self, key: K, value: V) -> None:
        self.entries[key] = value

    def get(self, key: K, fallback: V = None) -> V:
        if key in self.entries:
            return self.entries[key]
        return fallback

    @classmethod
    def of(cls, key: K, value: V):
        built = cls()
        built.put(key, value)
        return built

    def pairs(self):
        for key in sorted(self.entries):
            yield key, self.entries[key]


registry = Registry.of("first", 1)
registry.put("second", 2)
print("registry:", registry.get("first"), registry.get("missing"),
      registry.get("missing", 0))
print("registry pairs:", tuple(registry.pairs()))
