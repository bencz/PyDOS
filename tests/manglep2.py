"""Comprehensive Python 3.12 language and data-model demonstration.

Run with:
    python3.12 manglepy.py

The file calls ``run_all_tests()`` directly at the end.
"""

import sys
from collections.abc import Buffer, Callable, Generator, Iterator
from copy import copy, deepcopy
from typing import TypedDict, Unpack, override


if sys.version_info < (3, 12):
    raise RuntimeError("This file requires Python 3.12 or newer.")


class TestSuite:
    """Small dependency-free test runner used by this demonstration."""

    def __init__(self) -> None:
        self.passed = 0
        self.failed = 0
        self.failures: list[tuple[str, BaseException]] = []

    def run(self, name: str, test: Callable[[], None]) -> None:
        try:
            test()
        except BaseException as error:
            self.failed += 1
            self.failures.append((name, error))
            print(f"[FAIL] {name}: {type(error).__name__}: {error}")
        else:
            self.passed += 1
            print(f"[PASS] {name}")

    def finish(self) -> None:
        total = self.passed + self.failed
        print("\n" + "=" * 72)
        print(f"Completed {total} tests: {self.passed} passed, {self.failed} failed.")

        if self.failures:
            details = "\n".join(
                f"{index}. {name}: {type(error).__name__}: {error}"
                for index, (name, error) in enumerate(self.failures, start=1)
            )
            raise AssertionError(f"Python 3.12 demonstration failures:\n{details}")


def require(condition: object, message: str) -> None:
    """Raise a readable assertion when a demonstration condition is false."""

    if not condition:
        raise AssertionError(message)


# ---------------------------------------------------------------------------
# PEP 695: type parameter syntax, generic aliases, TypeVarTuple and ParamSpec
# ---------------------------------------------------------------------------


type Pair[T] = tuple[T, T]
type LabeledTuple[*Ts] = tuple[str, *Ts]
type Tree[T] = T | tuple[Tree[T], Tree[T]]
type IntegerCallable[**P] = Callable[P, int]


class Box[T]:
    """Generic container using Python 3.12 type parameter syntax."""

    def __init__(self, value: T) -> None:
        self.value = value

    def map[U](self, transform: Callable[[T], U]) -> "Box[U]":
        return Box(transform(self.value))

    def __repr__(self) -> str:
        return f"Box({self.value!r})"


class StringBox[T: str](Box[T]):
    """Generic class with a lazily evaluated upper bound."""


class NumericBox[T: (int, float)](Box[T]):
    """Generic class whose type parameter has explicit constraints."""


# Entity is deliberately declared later. Creating Repository succeeds because
# PEP 695 evaluates this bound lazily when __bound__ is accessed.
class Repository[T: Entity]:
    def __init__(self) -> None:
        self._items: list[T] = []

    def add(self, item: T) -> None:
        self._items.append(item)

    def all(self) -> tuple[T, ...]:
        return tuple(self._items)


class Entity:
    def __init__(self, identifier: int) -> None:
        self.identifier = identifier

    def __repr__(self) -> str:
        return f"Entity(identifier={self.identifier})"


def first[T](items: tuple[T, ...]) -> T:
    if not items:
        raise ValueError("items must not be empty")
    return items[0]


def rotate_first[T, *Ts](items: tuple[T, *Ts]) -> tuple[*Ts, T]:
    return (*items[1:], items[0])


def invoke[T, **P](
    callback: Callable[P, T],
    *args: P.args,
    **kwargs: P.kwargs,
) -> T:
    return callback(*args, **kwargs)


class GenericVault[__T]:
    """Private-looking type parameter combined with class name mangling."""

    def __init__(self, value: __T) -> None:
        self.__value = value

    def reveal(self) -> __T:
        return self.__value

    @classmethod
    def type_parameter_details(cls) -> tuple[str, object]:
        parameter, = cls.__type_params__
        return parameter.__name__, parameter


# ---------------------------------------------------------------------------
# Name mangling, inheritance, slots, descriptors and attribute categories
# ---------------------------------------------------------------------------


class BaseVault:
    __class_secret = "base-class-secret"

    def __init__(self) -> None:
        self.__instance_secret = "base-instance-secret"

    def base_secret(self) -> str:
        return self.__instance_secret

    @classmethod
    def base_class_secret(cls) -> str:
        return cls.__class_secret


class DerivedVault(BaseVault):
    __class_secret = "derived-class-secret"

    def __init__(self) -> None:
        super().__init__()
        self.__instance_secret = "derived-instance-secret"

    def derived_secret(self) -> str:
        return self.__instance_secret

    @classmethod
    def derived_class_secret(cls) -> str:
        return cls.__class_secret


class ManglingCases:
    __private = "mangled"
    __private_ = "also-mangled"
    __special__ = "not-mangled"
    _protected_by_convention = "not-mangled"
    public = "not-mangled"


class OuterScope:
    __value = "outer"

    class InnerScope:
        __value = "inner"

        def reveal(self) -> str:
            return self.__value

    @classmethod
    def reveal(cls) -> str:
        return cls.__value


class SlottedSecret:
    __slots__ = ("public", "_conventional", "__private")

    def __init__(self) -> None:
        self.public = "public"
        self._conventional = "conventional"
        self.__private = "private"

    def reveal(self) -> str:
        return self.__private


class NonNegative:
    """Descriptor demonstrating __set_name__, __get__ and __set__."""

    def __set_name__(self, owner: type, name: str) -> None:
        self.storage_name = f"_{owner.__name__}{name}"

    def __get__(self, instance: object, owner: type | None = None) -> "int | NonNegative":
        if instance is None:
            return self
        return getattr(instance, self.storage_name, 0)

    def __set__(self, instance: object, value: int) -> None:
        if value < 0:
            raise ValueError("value must be non-negative")
        setattr(instance, self.storage_name, value)


class ManagedScore:
    __score = NonNegative()

    def __init__(self, score: int) -> None:
        self.__score = score

    @property
    def score(self) -> int:
        return self.__score

    @score.setter
    def score(self, value: int) -> None:
        self.__score = value


class FeatureCollection[T]:
    """Idiomatic class with many common class and data-model features."""

    instance_count = 0
    category = "collection"
    __slots__ = ("_items", "__label")

    def __init__(self, items: list[T], label: str = "items") -> None:
        type(self).instance_count += 1
        self._items = list(items)
        self.__label = self.normalize_label(label)

    @staticmethod
    def normalize_label(value: str) -> str:
        return " ".join(value.strip().split()).title()

    @classmethod
    def from_iterable[U](cls, items: Iterator[U], label: str = "generated") -> "FeatureCollection[U]":
        return cls(list(items), label)

    @property
    def label(self) -> str:
        return self.__label

    @label.setter
    def label(self, value: str) -> None:
        self.__label = self.normalize_label(value)

    def __iter__(self) -> Iterator[T]:
        return iter(self._items)

    def __len__(self) -> int:
        return len(self._items)

    def __contains__(self, item: object) -> bool:
        return item in self._items

    def __getitem__(self, index: int | slice) -> T | list[T]:
        return self._items[index]

    def __call__[U](self, transform: Callable[[T], U]) -> tuple[U, ...]:
        return tuple(transform(item) for item in self._items)

    def __repr__(self) -> str:
        return f"{type(self).__name__}(items={self._items!r}, label={self.label!r})"


# ---------------------------------------------------------------------------
# Nested functions, lambdas, closures and generator protocols
# ---------------------------------------------------------------------------


class NestedMethodDemonstration:
    def build_pipeline(self, offset: int) -> Callable[[int], int]:
        def level_one(multiplier: int) -> Callable[[int], int]:
            def level_two(increment: int) -> Callable[[int], int]:
                def level_three(power: int) -> Callable[[int], int]:
                    def level_four(value: int) -> int:
                        normalize = lambda number: number + offset
                        return (normalize(value) * multiplier + increment) ** power

                    return level_four

                return level_three(2)

            return level_two(3)

        return level_one(2)


def create_counter(initial: int = 0) -> Callable[[int], int]:
    count = initial

    def increment(step: int = 1) -> int:
        nonlocal count
        count += step
        return count

    return increment


def child_generator() -> Generator[int, None, int]:
    yield 1
    yield 2
    return 99


def delegating_generator() -> Generator[int, None, str]:
    child_result = yield from child_generator()
    yield child_result
    return "delegation-complete"


def accumulating_generator() -> Generator[int, int | None, int]:
    total = 0

    while True:
        value = yield total
        if value is None:
            return total
        total += value


# ---------------------------------------------------------------------------
# Pattern matching, context managers and idiomatic expressions
# ---------------------------------------------------------------------------


class Point:
    __match_args__ = ("x", "y")

    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y


def describe_pattern(value: object) -> str:
    match value:
        case Point(0, 0):
            return "origin"
        case Point(x, y) if x == y:
            return f"diagonal:{x}"
        case (first_value, second_value, *remaining):
            return f"tuple:{first_value}:{second_value}:{len(remaining)}"
        case {"name": str(name), "active": True}:
            return f"active:{name}"
        case _:
            return "unknown"


class Transaction:
    def __init__(self, log: list[str]) -> None:
        self.log = log

    def __enter__(self) -> "Transaction":
        self.log.append("enter")
        return self

    def record(self, value: str) -> None:
        self.log.append(value)

    def __exit__(
        self,
        exception_type: type[BaseException] | None,
        exception: BaseException | None,
        traceback: object | None,
    ) -> bool:
        self.log.append("exit")
        return False


# ---------------------------------------------------------------------------
# Deeper data model: allocation, subclass hooks and attribute interception
# ---------------------------------------------------------------------------


class NormalizedText:
    def __new__(cls, value: str) -> "NormalizedText":
        instance = super().__new__(cls)
        instance.value = " ".join(value.strip().lower().split())
        return instance

    def __repr__(self) -> str:
        return f"NormalizedText({self.value!r})"


class Plugin:
    registry: dict[str, type["Plugin"]] = {}
    plugin_key = ""

    def __init_subclass__(cls, *, key: str, **kwargs: object) -> None:
        super().__init_subclass__(**kwargs)
        cls.plugin_key = key
        Plugin.registry[key] = cls


class JsonPlugin(Plugin, key="json"):
    pass


class FlexibleObject:
    def __init__(self, name: str) -> None:
        self.name = name
        self.temporary = "remove-me"

    def __getattribute__(self, name: str) -> object:
        if name == "upper_name":
            original = object.__getattribute__(self, "name")
            return original.upper()
        return object.__getattribute__(self, name)

    def __getattr__(self, name: str) -> str:
        return f"missing:{name}"

    def __setattr__(self, name: str, value: object) -> None:
        if name == "name" and (not isinstance(value, str) or not value.strip()):
            raise ValueError("name must be a non-empty string")
        object.__setattr__(self, name, value)

    def __delattr__(self, name: str) -> None:
        if name == "name":
            raise AttributeError("name cannot be deleted")
        object.__delattr__(self, name)


class RichValue:
    def __init__(self, values: list[int]) -> None:
        self.values = list(values)

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, RichValue):
            return NotImplemented
        return self.values == other.values

    def __hash__(self) -> int:
        return hash(tuple(self.values))

    def __bool__(self) -> bool:
        return any(self.values)

    def __reversed__(self) -> Iterator[int]:
        return iter(self.values[::-1])

    def __format__(self, format_spec: str) -> str:
        separator = format_spec or ","
        return separator.join(str(value) for value in self.values)

    def __bytes__(self) -> bytes:
        return bytes(self.values)

    def __copy__(self) -> "RichValue":
        return type(self)(self.values)

    def __deepcopy__(self, memo: dict[int, object]) -> "RichValue":
        duplicate = type(self)(deepcopy(self.values, memo))
        memo[id(self)] = duplicate
        return duplicate


class DefaultDictionary(dict[str, int]):
    def __missing__(self, key: str) -> int:
        value = len(key)
        self[key] = value
        return value


# ---------------------------------------------------------------------------
# Cooperative multiple inheritance and method-resolution order
# ---------------------------------------------------------------------------


class RootProcessor:
    def process(self) -> tuple[str, ...]:
        return ("RootProcessor",)


class LeftProcessor(RootProcessor):
    def process(self) -> tuple[str, ...]:
        return ("LeftProcessor", *super().process())


class RightProcessor(RootProcessor):
    def process(self) -> tuple[str, ...]:
        return ("RightProcessor", *super().process())


class DiamondProcessor(LeftProcessor, RightProcessor):
    def process(self) -> tuple[str, ...]:
        return ("DiamondProcessor", *super().process())


# ---------------------------------------------------------------------------
# PEP 698 override and PEP 692 TypedDict + Unpack for **kwargs
# ---------------------------------------------------------------------------


class Renderer:
    def render(self, value: object) -> str:
        return str(value)


class HtmlRenderer(Renderer):
    @override
    def render(self, value: object) -> str:
        return f"<span>{value}</span>"


class RequestOptions(TypedDict):
    timeout: float
    retries: int


def configure_request(**options: Unpack[RequestOptions]) -> RequestOptions:
    return options


# ---------------------------------------------------------------------------
# Buffer protocol made accessible to Python classes by PEP 688
# ---------------------------------------------------------------------------


class ByteStorage:
    def __init__(self, data: bytes) -> None:
        self.__data = bytearray(data)
        self.__active_views = 0

    def __buffer__(self, flags: int, /) -> memoryview:
        self.__active_views += 1
        return memoryview(self.__data)

    def __release_buffer__(self, view: memoryview, /) -> None:
        self.__active_views -= 1

    @property
    def active_views(self) -> int:
        return self.__active_views


# ---------------------------------------------------------------------------
# Individual test groups
# ---------------------------------------------------------------------------


def test_pep695_generics_and_aliases() -> None:
    box = Box(10)
    mapped = box.map(lambda value: f"value={value}")

    require(box.value == 10, "generic Box did not retain its value")
    require(mapped.value == "value=10", "generic map returned the wrong value")
    require(first(("alpha", "beta")) == "alpha", "generic first() failed")
    require(rotate_first((1, "two", 3.0)) == ("two", 3.0, 1), "variadic tuple rotation failed")
    require(invoke(lambda left, right=0: left + right, 7, right=5) == 12, "ParamSpec invocation failed")

    require(Box.__type_params__[0].__name__ == "T", "Box type parameter was not exposed")
    require(first.__type_params__[0].__name__ == "T", "function type parameter was not exposed")
    require(Pair.__name__ == "Pair", "type alias name was not exposed")
    require(Pair.__type_params__[0].__name__ == "T", "type alias parameter was not exposed")
    require(Pair.__value__ == tuple[Pair.__type_params__[0], Pair.__type_params__[0]], "Pair alias value is unexpected")
    require(Tree.__name__ == "Tree", "recursive alias name is incorrect")
    require(len(LabeledTuple.__type_params__) == 1, "TypeVarTuple was not created")
    require(IntegerCallable.__type_params__[0].__name__ == "P", "ParamSpec alias was not created")

    string_parameter, = StringBox.__type_params__
    numeric_parameter, = NumericBox.__type_params__
    repository_parameter, = Repository.__type_params__

    require(string_parameter.__bound__ is str, "StringBox bound is incorrect")
    require(numeric_parameter.__constraints__ == (int, float), "NumericBox constraints are incorrect")
    require(repository_parameter.__bound__ is Entity, "lazy Repository bound did not resolve")

    repository = Repository()
    entity = Entity(42)
    repository.add(entity)
    require(repository.all() == (entity,), "generic Repository failed")


def test_private_generic_parameter() -> None:
    vault = GenericVault("secret")
    parameter_name, parameter = GenericVault.type_parameter_details()

    require(vault.reveal() == "secret", "GenericVault did not retain its value")
    require(parameter_name == "__T", "private type parameter name changed unexpectedly")
    require(parameter is GenericVault.__type_params__[0], "type parameter identity changed")
    require("_GenericVault__value" in vars(vault), "private instance attribute was not mangled")


def test_name_mangling() -> None:
    vault = DerivedVault()

    require(vault.base_secret() == "base-instance-secret", "base private attribute collided")
    require(vault.derived_secret() == "derived-instance-secret", "derived private attribute collided")
    require(vault.base_class_secret() == "base-class-secret", "base private class attribute collided")
    require(vault.derived_class_secret() == "derived-class-secret", "derived private class attribute collided")
    require(vault._BaseVault__instance_secret == "base-instance-secret", "base mangled attribute is missing")
    require(vault._DerivedVault__instance_secret == "derived-instance-secret", "derived mangled attribute is missing")

    names = vars(ManglingCases)
    require("_ManglingCases__private" in names, "double-underscore name was not mangled")
    require("_ManglingCases__private_" in names, "single trailing underscore prevented mangling")
    require("__special__" in names, "dunder name should not be mangled")
    require("_protected_by_convention" in names, "single-underscore name should remain unchanged")

    require(OuterScope.reveal() == "outer", "outer private value failed")
    require(OuterScope.InnerScope().reveal() == "inner", "nested class private value failed")
    require("_OuterScope__value" in vars(OuterScope), "outer mangled name is missing")
    require("_InnerScope__value" in vars(OuterScope.InnerScope), "inner mangled name is missing")

    slotted = SlottedSecret()
    require(slotted.reveal() == "private", "private slotted attribute failed")
    require(not hasattr(slotted, "__dict__"), "slotted instance unexpectedly has __dict__")
    require(hasattr(slotted, "_SlottedSecret__private"), "private slot descriptor was not mangled")


def test_descriptor_property_static_and_class_methods() -> None:
    score = ManagedScore(10)
    score.score = 25
    require(score.score == 25, "property or descriptor assignment failed")

    try:
        score.score = -1
    except ValueError:
        pass
    else:
        raise AssertionError("descriptor accepted a negative value")

    collection = FeatureCollection([1, 2, 3], "  sample   values ")
    generated = FeatureCollection.from_iterable(iter((4, 5)), " generated values ")

    require(collection.label == "Sample Values", "staticmethod normalization failed")
    require(generated.label == "Generated Values", "classmethod constructor failed")
    require(tuple(collection) == (1, 2, 3), "iteration protocol failed")
    require(len(collection) == 3, "length protocol failed")
    require(2 in collection, "containment protocol failed")
    require(collection[0] == 1 and collection[1:] == [2, 3], "indexing or slicing failed")
    require(collection(lambda value: value * 10) == (10, 20, 30), "callable instance failed")
    require("FeatureCollection" in repr(collection), "representation protocol failed")


def test_nested_functions_lambdas_and_closures() -> None:
    pipeline = NestedMethodDemonstration().build_pipeline(offset=1)
    require(pipeline(4) == 169, "four-level nested pipeline returned the wrong result")

    counter = create_counter(10)
    require(counter.__code__.co_freevars == ("count",), "closure free variable is incorrect")
    require(counter.__closure__ is not None, "closure cells are missing")
    require(counter.__closure__[0].cell_contents == 10, "initial closure cell value is incorrect")
    require(counter(5) == 15, "nonlocal update failed")
    require(counter.__closure__[0].cell_contents == 15, "closure cell was not updated")

    values = [5, 2, 9, 1]
    sorted_values = sorted(values, key=lambda value: (value % 2, value))
    require(sorted_values == [2, 1, 5, 9], "lambda-based sorting failed")


def test_generators_yield_from_send_and_return() -> None:
    delegated = delegating_generator()
    require(next(delegated) == 1, "first delegated yield is incorrect")
    require(next(delegated) == 2, "second delegated yield is incorrect")
    require(next(delegated) == 99, "yield from return value was not captured")

    try:
        next(delegated)
    except StopIteration as stopped:
        require(stopped.value == "delegation-complete", "generator return value is incorrect")
    else:
        raise AssertionError("delegating generator did not stop")

    accumulator = accumulating_generator()
    require(next(accumulator) == 0, "generator priming value is incorrect")
    require(accumulator.send(5) == 5, "first send() result is incorrect")
    require(accumulator.send(7) == 12, "second send() result is incorrect")

    try:
        accumulator.send(None)
    except StopIteration as stopped:
        require(stopped.value == 12, "accumulator return value is incorrect")
    else:
        raise AssertionError("accumulator did not stop")


def test_python312_fstrings() -> None:
    person = {"name": "Ada", "languages": ("Python", "C")}

    same_quotes = f"{person["name"]}"
    multiline_expression = f"""Total: {
        sum(
            number
            for number in (10, 20, 30)
            # Python 3.12 permits comments inside f-string expressions.
        )
    }"""
    nested = f"Result: {f"{person["name"]}: {len(person["languages"])}"}"
    backslash_expression = f"Languages: {"\n".join(person["languages"])}"

    require(same_quotes == "Ada", "same-quote f-string expression failed")
    require(multiline_expression.strip() == "Total: 60", "multiline f-string expression failed")
    require(nested == "Result: Ada: 2", "nested f-string failed")
    require(backslash_expression == "Languages: Python\nC", "backslash in f-string expression failed")


def test_comprehension_inlining_and_idioms() -> None:
    outer_value = "visible"
    item = "outside"

    snapshots = [
        (item, locals().get("outer_value"), "item" in locals())
        for item in (10, 20)
    ]
    doubled = [result for number in range(6) if (result := number * 2) > 4]
    mapping = {key: value for key, value in zip(("a", "b"), (1, 2), strict=True)}
    flattened = [value for group in ((1, 2), (3, 4)) for value in group]

    require(item == "outside", "comprehension iteration variable leaked")
    require(snapshots == [(10, "visible", True), (20, "visible", True)], "inlined locals() behavior is incorrect")
    require(doubled == [6, 8, 10], "walrus operator in comprehension failed")
    require(result == 10, "walrus target should remain available after the comprehension")
    require(mapping == {"a": 1, "b": 2}, "strict zip or dict comprehension failed")
    require(flattened == [1, 2, 3, 4], "nested comprehension failed")

    nested_code_objects = [
        constant.co_name
        for constant in test_comprehension_inlining_and_idioms.__code__.co_consts
        if hasattr(constant, "co_name")
    ]
    require("<listcomp>" not in nested_code_objects, "list comprehension was not inlined")
    require("<dictcomp>" not in nested_code_objects, "dict comprehension was not inlined")


def test_pattern_matching_and_context_manager() -> None:
    require(describe_pattern(Point(0, 0)) == "origin", "origin pattern failed")
    require(describe_pattern(Point(4, 4)) == "diagonal:4", "guarded class pattern failed")
    require(describe_pattern((1, 2, 3, 4)) == "tuple:1:2:2", "sequence pattern failed")
    require(describe_pattern({"name": "Ada", "active": True}) == "active:Ada", "mapping pattern failed")

    log: list[str] = []
    with Transaction(log) as transaction:
        transaction.record("work")

    require(log == ["enter", "work", "exit"], "context manager protocol failed")


def test_data_model_hooks() -> None:
    normalized = NormalizedText("  Hello   PYTHON  ")
    require(normalized.value == "hello python", "__new__ normalization failed")

    require(Plugin.registry["json"] is JsonPlugin, "__init_subclass__ registration failed")
    require(JsonPlugin.plugin_key == "json", "subclass keyword handling failed")

    flexible = FlexibleObject("Ada")
    require(flexible.upper_name == "ADA", "__getattribute__ computed attribute failed")
    require(flexible.unknown == "missing:unknown", "__getattr__ fallback failed")
    flexible.name = "Grace"
    require(flexible.name == "Grace", "__setattr__ assignment failed")
    del flexible.temporary
    require(flexible.temporary == "missing:temporary", "__delattr__ deletion failed")

    try:
        del flexible.name
    except AttributeError:
        pass
    else:
        raise AssertionError("protected attribute deletion was allowed")

    first_value = RichValue([1, 2, 3])
    equal_value = RichValue([1, 2, 3])
    empty_value = RichValue([0, 0])
    shallow = copy(first_value)
    deep = deepcopy(first_value)

    require(first_value == equal_value, "__eq__ failed")
    require(hash(first_value) == hash(equal_value), "__hash__ failed")
    require(bool(first_value) and not bool(empty_value), "__bool__ failed")
    require(tuple(reversed(first_value)) == (3, 2, 1), "__reversed__ failed")
    require(f"{first_value:|}" == "1|2|3", "__format__ failed")
    require(bytes(first_value) == b"\x01\x02\x03", "__bytes__ failed")
    require(shallow == first_value and shallow is not first_value, "__copy__ failed")
    require(deep == first_value and deep is not first_value, "__deepcopy__ failed")
    require(deep.values is not first_value.values, "deep copy retained the original list")

    defaults = DefaultDictionary()
    require(defaults["python"] == 6, "__missing__ returned the wrong value")
    require(defaults["python"] == 6 and "python" in defaults, "__missing__ did not cache the value")


def test_multiple_inheritance_and_mro() -> None:
    processor = DiamondProcessor()
    result = processor.process()
    expected = ("DiamondProcessor", "LeftProcessor", "RightProcessor", "RootProcessor")

    require(result == expected, "cooperative super() chain is incorrect")
    require(
        DiamondProcessor.__mro__[:4]
        == (DiamondProcessor, LeftProcessor, RightProcessor, RootProcessor),
        "diamond MRO is incorrect",
    )


def test_override_typeddict_and_unpack() -> None:
    renderer = HtmlRenderer()
    options = configure_request(timeout=1.5, retries=3)

    require(renderer.render("value") == "<span>value</span>", "overridden method failed")
    require(getattr(HtmlRenderer.render, "__override__", False) is True, "@override marker is missing")
    require(options == {"timeout": 1.5, "retries": 3}, "TypedDict kwargs function failed")
    require(hasattr(RequestOptions, "__orig_bases__"), "TypedDict __orig_bases__ is missing")
    require("kwargs" not in configure_request.__annotations__, "unexpected kwargs annotation key")
    require("options" in configure_request.__annotations__, "Unpack annotation is missing")
    require("Unpack" in repr(configure_request.__annotations__["options"]), "Unpack annotation is unexpected")


def test_buffer_protocol() -> None:
    storage = ByteStorage(b"Python 3.12")

    require(isinstance(storage, Buffer), "ByteStorage is not recognized as a Buffer")
    view = memoryview(storage)

    require(storage.active_views == 1, "__buffer__ did not record an active view")
    require(view.tobytes() == b"Python 3.12", "buffer bytes are incorrect")
    require(view[0] == ord("P"), "buffer indexing failed")

    view.release()
    require(storage.active_views == 0, "__release_buffer__ did not release the view")


def test_sys_monitoring() -> None:
    monitoring = sys.monitoring
    available_tool_ids = [tool_id for tool_id in range(6) if monitoring.get_tool(tool_id) is None]
    require(available_tool_ids, "no free sys.monitoring tool identifier is available")

    tool_id = available_tool_ids[-1]
    events: list[tuple[str, str, object]] = []

    def monitored_target(left: int, right: int) -> int:
        return left + right

    def on_start(code: object, instruction_offset: int) -> None:
        events.append(("start", code.co_name, instruction_offset))

    def on_return(code: object, instruction_offset: int, return_value: object) -> None:
        events.append(("return", code.co_name, return_value))

    monitoring.use_tool_id(tool_id, "manglepy-demo")

    try:
        monitoring.register_callback(tool_id, monitoring.events.PY_START, on_start)
        monitoring.register_callback(tool_id, monitoring.events.PY_RETURN, on_return)
        monitoring.set_local_events(
            tool_id,
            monitored_target.__code__,
            monitoring.events.PY_START | monitoring.events.PY_RETURN,
        )

        require(monitored_target(7, 8) == 15, "monitored target returned the wrong value")
        require(any(event[0] == "start" for event in events), "PY_START event was not received")
        require(any(event == ("return", "monitored_target", 15) for event in events), "PY_RETURN event was not received")
    finally:
        monitoring.set_local_events(tool_id, monitored_target.__code__, monitoring.events.NO_EVENTS)
        monitoring.register_callback(tool_id, monitoring.events.PY_START, None)
        monitoring.register_callback(tool_id, monitoring.events.PY_RETURN, None)
        monitoring.free_tool_id(tool_id)


def run_all_tests() -> None:
    print(f"Python runtime: {sys.version.split()[0]}")
    print("Running Python 3.12 feature demonstrations...\n")

    suite = TestSuite()
    tests: tuple[tuple[str, Callable[[], None]], ...] = (
        ("PEP 695 generics and type aliases", test_pep695_generics_and_aliases),
        ("private generic parameter", test_private_generic_parameter),
        ("name mangling and slots", test_name_mangling),
        ("descriptor, property, staticmethod and classmethod", test_descriptor_property_static_and_class_methods),
        ("nested functions, lambdas and closures", test_nested_functions_lambdas_and_closures),
        ("generators, yield from, send and return", test_generators_yield_from_send_and_return),
        ("Python 3.12 f-strings", test_python312_fstrings),
        ("comprehension inlining and idioms", test_comprehension_inlining_and_idioms),
        ("pattern matching and context manager", test_pattern_matching_and_context_manager),
        ("deep data-model hooks", test_data_model_hooks),
        ("multiple inheritance and MRO", test_multiple_inheritance_and_mro),
        ("override, TypedDict and Unpack", test_override_typeddict_and_unpack),
        ("Python-level buffer protocol", test_buffer_protocol),
        ("sys.monitoring", test_sys_monitoring),
    )

    for name, test in tests:
        suite.run(name, test)

    suite.finish()


run_all_tests()
