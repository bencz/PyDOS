"""Idiomatic Python 3.12 feature demonstration with no imports."""


type Pair[T] = tuple[T, T]
type Record[T] = tuple[str, T]
type Result[T] = tuple[bool, T | None, str]
type LabeledTuple[*Ts] = tuple[str, *Ts]


class NonNegativeInteger:
    """Descriptor that accepts only non-negative integers."""

    def __set_name__(self, owner: type, name: str) -> None:
        self.public_name = name
        self.storage_name = f"_{owner.__name__}__validated_{name}"

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return getattr(instance, self.storage_name, 0)

    def __set__(self, instance, value: int) -> None:
        if not isinstance(value, int) or isinstance(value, bool) or value < 0:
            raise ValueError(f"{self.public_name} must be a non-negative integer")
        setattr(instance, self.storage_name, value)


class Box[T]:
    """Generic container using Python 3.12 type-parameter syntax."""

    __slots__ = ("__value", "_label")
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

    def transform[U](self, function) -> "Box[U]":
        return Box(function(self.__value), f"transformed:{self._label}")

    def filter(self, predicate) -> Result[T]:
        accepted = bool(predicate(self.__value))
        return (
            True,
            self.__value,
            "value accepted",
        ) if accepted else (
            False,
            None,
            "value rejected",
        )

    def generate(self, repetitions: int = 1):
        for index in range(repetitions):
            yield index, self.__value

    def generate_transformed(self, *functions):
        yield self.__value
        yield from (function(self.__value) for function in functions)

    def nested_pipeline(self, *operations):
        """Use four nested function levels and a lexical closure."""
        call_count = 0

        def level_1(value):
            prefix = self._label

            def level_2(index, operation):
                def level_3(item):
                    factor = index + 1

                    def level_4():
                        nonlocal call_count
                        call_count += 1
                        result = operation(item)
                        return f"{prefix}[{factor}]={result!r}"

                    return level_4()

                return level_3(value)

            return tuple(
                level_2(index, operation)
                for index, operation in enumerate(operations)
            )

        return level_1(self.__value), call_count

    def __iter__(self):
        yield self._label
        yield self.__value

    def __len__(self) -> int:
        try:
            return len(self.__value)
        except TypeError:
            return 1

    def __contains__(self, item: object) -> bool:
        try:
            return item in self.__value
        except TypeError:
            return item == self.__value

    def __getitem__(self, index):
        return self.__value[index]

    def __call__(self, function):
        return function(self.__value)

    def __repr__(self) -> str:
        return (
            f"{type(self).__name__}("
            f"value={self.__value!r}, label={self._label!r})"
        )


class Vault:
    """Base class demonstrating several name-mangling cases."""

    __class_secret = "base-class secret"
    _non_public_attribute = "convention only"
    public_attribute = "public API"

    def __init__(self, secret: str) -> None:
        self.__secret = secret

    def __private_method(self) -> str:
        return f"base:{self.__secret}"

    def reveal_base_secret(self) -> str:
        return self.__private_method()

    @classmethod
    def class_secret(cls) -> str:
        return cls.__class_secret

    @staticmethod
    def base_mangled_name(name: str) -> str:
        return f"_Vault__{name.lstrip('_')}"


class SpecialVault(Vault):
    """Reuse private names without overriding private names from the base class."""

    __class_secret = "subclass secret"

    def __init__(self, base_secret: str, special_secret: str) -> None:
        super().__init__(base_secret)
        self.__secret = special_secret

    def __private_method(self) -> str:
        return f"subclass:{self.__secret}"

    def reveal_special_secret(self) -> str:
        return self.__private_method()

    @classmethod
    def subclass_secret(cls) -> str:
        return cls.__class_secret


class ContextCounter:
    """Demonstrate descriptors, slots, context management, and properties."""

    __slots__ = ("name", "__validated_value", "__active")

    value = NonNegativeInteger()
    instance_count = 0

    def __init__(self, name: str, value: int = 0) -> None:
        self.name = name
        self.value = value
        self.__active = False
        type(self).instance_count += 1

    @property
    def active(self) -> bool:
        return self.__active

    def increment(self, step: int = 1) -> int:
        self.value += step
        return self.value

    def __enter__(self):
        self.__active = True
        return self

    def __exit__(self, error_type, error, traceback) -> bool:
        self.__active = False
        return False

    def __repr__(self) -> str:
        return (
            f"ContextCounter(name={self.name!r}, value={self.value}, "
            f"active={self.active})"
        )


def first[T](values: tuple[T, ...]) -> T:
    if not values:
        raise ValueError("the tuple cannot be empty")
    return values[0]


def swap[T](pair: Pair[T]) -> Pair[T]:
    left, right = pair
    return right, left


def label_values[*Ts](label: str, *values: *Ts) -> LabeledTuple[*Ts]:
    return label, *values


def analyze_tuple(value: tuple) -> str:
    match value:
        case ():
            return "empty tuple"
        case (single,):
            return f"single-item tuple: {single!r}"
        case (first_value, second_value):
            return f"pair: {first_value!r} and {second_value!r}"
        case (head, *middle, tail):
            return f"head={head!r}, middle={middle!r}, tail={tail!r}"


def accumulator(initial: int = 0):
    """Receive values through send() and return the total when closed."""
    total = initial
    while True:
        received = yield total
        if received is None:
            return total
        total += received


def count_to(limit: int):
    yield from range(limit)


def create_multiplier(factor: int):
    return lambda value: factor * value


def run_demonstration() -> None:
    print("\n1. Generics, properties, and protocols")
    box = Box((2, 3, 5, 7), "primes")
    print(box)
    print("unpacked:", tuple(box))
    print("length:", len(box), "contains 5:", 5 in box, "index 2:", box[2])
    print("callable result:", box(lambda numbers: sum(numbers)))

    text_box = box.transform(lambda numbers: ",".join(str(n) for n in numbers))
    print("transformed:", text_box)
    print("filter:", box.filter(lambda numbers: all(n > 0 for n in numbers)))
    print("generator:", tuple(box.generate(3)))
    print(
        "yield and yield from:",
        tuple(box.generate_transformed(len, sum, lambda values: values[::-1])),
    )
    print("staticmethod:", Box.values_are_equal((1, 2), (1, 2)))
    print("classmethod:", Box.empty())
    print("class attribute:", Box.instances_created)

    print("\n2. Nested functions, lambdas, and closures")
    outputs, calls = box.nested_pipeline(
        lambda values: sum(values),
        lambda values: tuple(value * value for value in values),
        lambda values: {
            value: "even" if value % 2 == 0 else "odd"
            for value in values
        },
    )
    print("pipeline:", outputs)
    print("calls captured through nonlocal:", calls)
    double = create_multiplier(2)
    print("lambda closure:", tuple(map(double, (1, 2, 3))))

    print("\n3. Tuples and pattern matching")
    pair: Pair[int] = (10, 20)
    record: Record[int] = ("age", 17)
    print("swap:", swap(pair))
    print("first:", first(("a", "b", "c")))
    print("record:", record)
    print("variadic tuple:", label_values("data", 1, "two", 3.0, True))
    for item in ((), (1,), (1, 2), (1, 2, 3, 4, 5)):
        print(analyze_tuple(item))

    print("\n4. Name mangling with inheritance")
    vault = SpecialVault("ALPHA", "BETA")
    print(vault.reveal_base_secret())
    print(vault.reveal_special_secret())
    print("base class:", Vault.class_secret())
    print("inherited method keeps the base-class mangle:", SpecialVault.class_secret())
    print("subclass:", SpecialVault.subclass_secret())
    print("materialized private names:")
    print("  base attribute:", vault._Vault__secret)
    print("  subclass attribute:", vault._SpecialVault__secret)
    print("  base method:", vault._Vault__private_method())
    print("  subclass method:", vault._SpecialVault__private_method())
    print(
        "base class dictionary:",
        tuple(name for name in Vault.__dict__ if "secret" in name),
    )
    print(
        "subclass dictionary:",
        tuple(name for name in SpecialVault.__dict__ if "secret" in name),
    )

    try:
        print(vault.__secret)
    except AttributeError as error:
        print("direct access failed as expected:", error)

    print("\n5. Descriptor, slots, context manager, and property")
    counter = ContextCounter("tasks", 2)
    print("before:", counter)
    with counter as active_counter:
        print("inside context:", active_counter)
        print("incremented:", active_counter.increment(3))
    print("after:", counter)

    try:
        counter.value = -1
    except ValueError as error:
        print("descriptor validation:", error)

    try:
        counter.dynamic_attribute = "not allowed by __slots__"
    except AttributeError as error:
        print("slots blocked the attribute:", error)

    print("\n6. Generators with next(), send(), and yield from")
    generator = accumulator(10)
    print("initial:", next(generator))
    print("+5:", generator.send(5))
    print("+7:", generator.send(7))
    try:
        generator.send(None)
    except StopIteration as completion:
        print("generator final return value:", completion.value)
    print("yield from range:", tuple(count_to(5)))

    print("\n7. Comprehensions, walrus, sorting, and Python 3.12 f-strings")
    data = ("grape", "banana", "kiwi", "pineapple")
    lengths = {
        word: length
        for word in data
        if (length := len(word)) >= 4
    }
    ordered = tuple(sorted(data, key=lambda text: (len(text), text)))
    lines = [
        f"{word}={length}"
        for word, length in sorted(
            lengths.items(),
            key=lambda item: (item[1], item[0]),
        )
    ]
    report = f"Report:\n{"\n".join(lines)}"
    print("comprehension:", lengths)
    print("ordered:", ordered)
    print(report)

    print("\n8. Object pattern matching")
    match box:
        case Box(value=(first_number, *remaining), label=name):
            print(f"{name}: first={first_number}, remaining={remaining}")
        case _:
            print("box did not match")


run_demonstration()
