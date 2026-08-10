from abc import ABC, ABCMeta, abstractmethod, get_cache_token, update_abstractmethods


class TestFailure(AssertionError):
    """Raised when one of the local assertions fails."""


def check(condition: bool, message: str) -> None:
    """Raise a readable assertion failure when a condition is false."""
    if not condition:
        raise TestFailure(message)


def check_equal(actual: object, expected: object, message: str) -> None:
    """Compare two values and include both values in the failure message."""
    if actual != expected:
        raise TestFailure(
            f"{message}: expected {expected!r}, got {actual!r}"
        )


def expect_exception[
    E: BaseException,
](
    exception_type: type[E],
    callback,
    *,
    contains: str | None = None,
) -> E:
    """Run a callback and return the expected exception."""
    try:
        callback()
    except exception_type as error:
        if contains is not None:
            check(
                contains in str(error),
                f"Exception text does not contain {contains!r}: {error}",
            )
        return error
    except BaseException as error:
        raise TestFailure(
            f"Expected {exception_type.__name__}, got {type(error).__name__}: {error}"
        ) from error

    raise TestFailure(f"Expected {exception_type.__name__}, but nothing was raised")


TEST_RESULTS: list[tuple[str, bool, str]] = []


def run_test(name: str, callback) -> None:
    """Execute one test group without preventing later groups from running."""
    print(f"\n[{name}]")

    try:
        callback()
    except BaseException as error:
        TEST_RESULTS.append((name, False, f"{type(error).__name__}: {error}"))
        print(f"FAIL: {type(error).__name__}: {error}")
    else:
        TEST_RESULTS.append((name, True, ""))
        print("PASS")


# -----------------------------------------------------------------------------
# 1. Basic ABC behavior
# -----------------------------------------------------------------------------


class Shape(ABC):
    """Simple interface with one abstract operation."""

    @abstractmethod
    def area(self) -> float:
        """Return the shape area."""
        raise NotImplementedError


class Rectangle(Shape):
    def __init__(self, width: float, height: float) -> None:
        self.width = width
        self.height = height

    def area(self) -> float:
        return self.width * self.height


class IncompleteShape(Shape):
    """Still abstract because ``area`` is not overridden."""



def test_basic_abc_behavior() -> None:
    check(isinstance(Shape, ABCMeta), "Shape must be created by ABCMeta")
    check(issubclass(Rectangle, Shape), "Rectangle must be a Shape subclass")
    check("area" in Shape.__abstractmethods__, "Shape.area must be abstract")
    check(
        "area" in IncompleteShape.__abstractmethods__,
        "The abstract method must propagate to incomplete subclasses",
    )
    check_equal(Rectangle.__abstractmethods__, frozenset(), "Rectangle must be concrete")

    expect_exception(TypeError, Shape)
    expect_exception(TypeError, IncompleteShape)

    rectangle = Rectangle(4.0, 2.5)
    check(isinstance(rectangle, Shape), "Concrete instance must satisfy the ABC")
    check_equal(rectangle.area(), 10.0, "Concrete abstract-method implementation failed")


# -----------------------------------------------------------------------------
# 2. Abstract methods may provide reusable implementations
# -----------------------------------------------------------------------------


class Normalizer(ABC):
    @abstractmethod
    def normalize(self, value: str) -> str:
        """Provide shared behavior even though subclasses must override it."""
        return value.strip()


class LowerNormalizer(Normalizer):
    def normalize(self, value: str) -> str:
        return super().normalize(value).lower()



def test_abstract_method_with_body() -> None:
    expect_exception(TypeError, Normalizer)

    normalizer = LowerNormalizer()
    check_equal(
        normalizer.normalize("  PyThOn  "),
        "python",
        "Subclass must be able to call an abstract base implementation through super()",
    )


# -----------------------------------------------------------------------------
# 3. Abstract properties, getters and setters
# -----------------------------------------------------------------------------


class Identified(ABC):
    @property
    @abstractmethod
    def identifier(self) -> str:
        """Read-only abstract property."""
        raise NotImplementedError


class User(Identified):
    def __init__(self, identifier: str) -> None:
        self._identifier = identifier

    @property
    def identifier(self) -> str:
        return self._identifier


class MutableSetting(ABC):
    def __init__(self, value: int) -> None:
        self._value = value

    @property
    def value(self) -> int:
        return self._value

    @value.setter
    @abstractmethod
    def value(self, new_value: int) -> None:
        """Only the setter is abstract."""
        raise NotImplementedError


class ConcreteSetting(MutableSetting):
    @MutableSetting.value.setter
    def value(self, new_value: int) -> None:
        self._value = int(new_value)



def test_abstract_properties() -> None:
    check(
        Identified.identifier.__isabstractmethod__,
        "A property with an abstract getter must itself be abstract",
    )
    check(
        MutableSetting.value.__isabstractmethod__,
        "A property with an abstract setter must itself be abstract",
    )
    check("value" in MutableSetting.__abstractmethods__, "Abstract property is missing")

    expect_exception(TypeError, Identified)
    expect_exception(TypeError, MutableSetting, contains="abstract")

    user = User("user-42")
    check_equal(user.identifier, "user-42", "Concrete property implementation failed")

    setting = ConcreteSetting(10)
    setting.value = 25
    check_equal(setting.value, 25, "Concrete abstract setter failed")
    check_equal(
        ConcreteSetting.__abstractmethods__,
        frozenset(),
        "Overriding the abstract setter must make the subclass concrete",
    )


# -----------------------------------------------------------------------------
# 4. Abstract class methods and static methods
# -----------------------------------------------------------------------------


class Codec(ABC):
    @classmethod
    @abstractmethod
    def create(cls, prefix: str):
        """Build a concrete codec."""
        raise NotImplementedError

    @staticmethod
    @abstractmethod
    def validate(value: object) -> bool:
        """Validate values without requiring an instance."""
        raise NotImplementedError

    @abstractmethod
    def encode(self, value: str) -> str:
        raise NotImplementedError


class PrefixCodec(Codec):
    def __init__(self, prefix: str) -> None:
        self.prefix = prefix

    @classmethod
    def create(cls, prefix: str):
        return cls(prefix)

    @staticmethod
    def validate(value: object) -> bool:
        return isinstance(value, str) and bool(value)

    def encode(self, value: str) -> str:
        return f"{self.prefix}:{value}"



def test_abstract_class_and_static_methods() -> None:
    check(
        Codec.__dict__["create"].__isabstractmethod__,
        "Abstract classmethod descriptor must report itself as abstract",
    )
    check(
        Codec.__dict__["validate"].__isabstractmethod__,
        "Abstract staticmethod descriptor must report itself as abstract",
    )

    check_equal(
        Codec.__abstractmethods__,
        frozenset({"create", "validate", "encode"}),
        "Codec abstract method set is incorrect",
    )

    codec = PrefixCodec.create("demo")
    check(PrefixCodec.validate("abc"), "Concrete static method failed")
    check(not PrefixCodec.validate(""), "Static method validation should reject empty text")
    check_equal(codec.encode("value"), "demo:value", "Concrete codec failed")


# -----------------------------------------------------------------------------
# 5. Decorator order matters
# -----------------------------------------------------------------------------


def create_bad_classmethod_order() -> None:
    class BadClassMethodOrder(ABC):
        @abstractmethod
        @classmethod
        def build(cls):
            raise NotImplementedError



def create_bad_staticmethod_order() -> None:
    class BadStaticMethodOrder(ABC):
        @abstractmethod
        @staticmethod
        def validate(value):
            raise NotImplementedError



def create_bad_property_order() -> None:
    class BadPropertyOrder(ABC):
        @abstractmethod
        @property
        def value(self):
            raise NotImplementedError



def test_decorator_order() -> None:
    expect_exception(AttributeError, create_bad_classmethod_order)
    expect_exception(AttributeError, create_bad_staticmethod_order)
    expect_exception(AttributeError, create_bad_property_order)

    check(
        Codec.__dict__["create"].__isabstractmethod__,
        "Correct classmethod decorator order must work",
    )
    check(
        Codec.__dict__["validate"].__isabstractmethod__,
        "Correct staticmethod decorator order must work",
    )


# -----------------------------------------------------------------------------
# 6. Direct ABCMeta use and custom ABC metaclasses
# -----------------------------------------------------------------------------


class DirectInterface(metaclass=ABCMeta):
    @abstractmethod
    def execute(self) -> str:
        raise NotImplementedError


class ConcreteDirectInterface(DirectInterface):
    def execute(self) -> str:
        return "executed"


class TaggedABCMeta(ABCMeta):
    """ABCMeta subclass that attaches metadata during class creation."""

    def __new__(mcls, name, bases, namespace, **kwargs):
        cls = super().__new__(mcls, name, bases, namespace, **kwargs)
        cls.created_by_tagged_meta = True
        return cls


class TaggedInterface(metaclass=TaggedABCMeta):
    @abstractmethod
    def tag(self) -> str:
        raise NotImplementedError


class TaggedImplementation(TaggedInterface):
    def tag(self) -> str:
        return "tagged"



def test_abcmeta_directly() -> None:
    check(type(DirectInterface) is ABCMeta, "Direct metaclass must be ABCMeta")
    expect_exception(TypeError, DirectInterface)
    check_equal(
        ConcreteDirectInterface().execute(),
        "executed",
        "Direct ABCMeta subclass implementation failed",
    )

    check(
        isinstance(TaggedInterface, TaggedABCMeta),
        "Custom ABCMeta subclass must create the interface",
    )
    check(
        TaggedImplementation.created_by_tagged_meta,
        "Custom ABC metaclass metadata was not propagated",
    )
    check_equal(TaggedImplementation().tag(), "tagged", "Custom ABCMeta failed")


# -----------------------------------------------------------------------------
# 7. Virtual subclasses registered with ABC.register()
# -----------------------------------------------------------------------------


class Serializer(ABC):
    @abstractmethod
    def dumps(self, value: object) -> str:
        raise NotImplementedError


class LegacySerializer:
    def dumps(self, value: object) -> str:
        return repr(value)


class MethodlessLegacyType:
    """Intentionally does not satisfy Serializer's abstract API."""



def test_virtual_subclass_registration() -> None:
    token_before = get_cache_token()

    returned_class = Serializer.register(LegacySerializer)
    token_after_first_registration = get_cache_token()

    check(returned_class is LegacySerializer, "register() must return the registered class")
    check(
        token_after_first_registration != token_before,
        "ABC cache token must change after a new virtual subclass registration",
    )
    check(issubclass(LegacySerializer, Serializer), "Registered class must satisfy issubclass")
    check(
        isinstance(LegacySerializer(), Serializer),
        "Registered instance must satisfy isinstance",
    )
    check(
        Serializer not in LegacySerializer.__mro__,
        "Virtual registration must not modify the registered class MRO",
    )

    Serializer.register(MethodlessLegacyType)

    check(
        issubclass(MethodlessLegacyType, Serializer),
        "Virtual registration does not enforce abstract method implementation",
    )
    check(
        not hasattr(MethodlessLegacyType(), "dumps"),
        "ABC registration must not inject interface methods into virtual subclasses",
    )


# -----------------------------------------------------------------------------
# 8. Decorator-style virtual registration
# -----------------------------------------------------------------------------


class Renderable(ABC):
    @abstractmethod
    def render(self) -> str:
        raise NotImplementedError


@Renderable.register
class ThirdPartyWidget:
    def render(self) -> str:
        return "third-party-widget"



def test_register_as_decorator() -> None:
    widget = ThirdPartyWidget()
    check(isinstance(widget, Renderable), "Decorator registration must affect isinstance")
    check_equal(widget.render(), "third-party-widget", "Registered implementation failed")
    check(
        Renderable not in ThirdPartyWidget.__mro__,
        "Decorator registration must still be virtual",
    )


# -----------------------------------------------------------------------------
# 9. Structural recognition through __subclasshook__
# -----------------------------------------------------------------------------


class Closable(ABC):
    @abstractmethod
    def close(self) -> None:
        raise NotImplementedError

    @classmethod
    def __subclasshook__(cls, candidate):
        if cls is Closable:
            has_close = any("close" in base.__dict__ for base in candidate.__mro__)
            if has_close:
                return True
        return NotImplemented


class FileLikeResource:
    def close(self) -> None:
        self.closed = True


class InheritedCloseBase:
    def close(self) -> None:
        self.closed = True


class InheritedCloseResource(InheritedCloseBase):
    pass


class NonClosableResource:
    pass



def test_subclasshook() -> None:
    check(
        issubclass(FileLikeResource, Closable),
        "__subclasshook__ must recognize a structural subclass",
    )
    check(
        issubclass(InheritedCloseResource, Closable),
        "__subclasshook__ must inspect the candidate MRO",
    )
    check(
        isinstance(FileLikeResource(), Closable),
        "Structural subclassing must also affect isinstance",
    )
    check(
        not issubclass(NonClosableResource, Closable),
        "Unmatched classes must fall back to normal subclass checks",
    )
    check(
        Closable not in FileLikeResource.__mro__,
        "Structural subclass recognition must not modify MRO",
    )


# -----------------------------------------------------------------------------
# 10. Dynamically changing abstract methods with update_abstractmethods()
# -----------------------------------------------------------------------------


class DynamicService(ABC):
    def run(self) -> str:
        return "running"



def dynamically_required(self) -> str:
    raise NotImplementedError



def concrete_dynamic_method(self) -> str:
    return "dynamic-concrete"



def test_update_abstractmethods() -> None:
    check_equal(
        DynamicService.__abstractmethods__,
        frozenset(),
        "DynamicService should initially be concrete",
    )

    DynamicService.required = abstractmethod(dynamically_required)

    check_equal(
        DynamicService.__abstractmethods__,
        frozenset(),
        "Changing methods after class creation must not automatically recalculate abstraction",
    )

    stale_instance = DynamicService()
    check_equal(stale_instance.run(), "running", "Stale abstraction state changed unexpectedly")

    returned_class = update_abstractmethods(DynamicService)
    check(returned_class is DynamicService, "update_abstractmethods() must return the class")
    check_equal(
        DynamicService.__abstractmethods__,
        frozenset({"required"}),
        "Dynamic abstract method was not discovered",
    )
    expect_exception(TypeError, DynamicService)

    DynamicService.required = concrete_dynamic_method
    update_abstractmethods(DynamicService)

    check_equal(
        DynamicService.__abstractmethods__,
        frozenset(),
        "Replacing a dynamic abstract method must restore concreteness after recalculation",
    )
    check_equal(
        DynamicService().required(),
        "dynamic-concrete",
        "Dynamic concrete method failed",
    )


# -----------------------------------------------------------------------------
# 11. Existing subclasses are not automatically refreshed
# -----------------------------------------------------------------------------


class MutableBase(ABC):
    def original(self) -> str:
        return "original"


class ExistingChild(MutableBase):
    pass



def newly_abstract(self) -> str:
    raise NotImplementedError



def test_update_does_not_refresh_subclasses() -> None:
    MutableBase.new_requirement = abstractmethod(newly_abstract)
    update_abstractmethods(MutableBase)

    check(
        "new_requirement" in MutableBase.__abstractmethods__,
        "Base class should see the dynamically added abstract method",
    )
    check_equal(
        ExistingChild.__abstractmethods__,
        frozenset(),
        "Existing subclass must remain stale until explicitly updated",
    )

    ExistingChild()

    update_abstractmethods(ExistingChild)
    check(
        "new_requirement" in ExistingChild.__abstractmethods__,
        "Explicitly updating the existing subclass must discover the new requirement",
    )
    expect_exception(TypeError, ExistingChild)

    def implementation(self) -> str:
        return "implemented"

    ExistingChild.new_requirement = implementation
    update_abstractmethods(ExistingChild)
    check_equal(ExistingChild().new_requirement(), "implemented", "Refreshed subclass failed")


# -----------------------------------------------------------------------------
# 12. Multiple inheritance combines abstract requirements
# -----------------------------------------------------------------------------


class Loadable(ABC):
    @abstractmethod
    def load(self) -> str:
        raise NotImplementedError


class Saveable(ABC):
    @abstractmethod
    def save(self) -> str:
        raise NotImplementedError


class PartiallyPersistent(Loadable, Saveable):
    def load(self) -> str:
        return "loaded"


class PersistentObject(PartiallyPersistent):
    def save(self) -> str:
        return "saved"



def test_multiple_inheritance() -> None:
    check_equal(
        PartiallyPersistent.__abstractmethods__,
        frozenset({"save"}),
        "Only the unimplemented requirement should remain abstract",
    )
    expect_exception(TypeError, PartiallyPersistent)

    persistent = PersistentObject()
    check_equal(persistent.load(), "loaded", "Inherited concrete method failed")
    check_equal(persistent.save(), "saved", "Second ABC implementation failed")
    check_equal(
        PersistentObject.__abstractmethods__,
        frozenset(),
        "Fully implemented multiple-inheritance subclass must be concrete",
    )
    check(
        PersistentObject.__mro__[:4]
        == (PersistentObject, PartiallyPersistent, Loadable, Saveable),
        "Unexpected ABC multiple-inheritance MRO prefix",
    )


# -----------------------------------------------------------------------------
# 13. Cooperative abstract methods across a diamond hierarchy
# -----------------------------------------------------------------------------


class PipelineStage(ABC):
    @abstractmethod
    def process(self, value: str) -> tuple[str, ...]:
        return ("PipelineStage", value)


class PrefixStage(PipelineStage):
    @abstractmethod
    def process(self, value: str) -> tuple[str, ...]:
        return ("PrefixStage", *super().process(value))


class LoggingStage(PipelineStage):
    @abstractmethod
    def process(self, value: str) -> tuple[str, ...]:
        return ("LoggingStage", *super().process(value))


class ConcretePipeline(PrefixStage, LoggingStage):
    def process(self, value: str) -> tuple[str, ...]:
        return ("ConcretePipeline", *super().process(value))



def test_cooperative_abstract_methods() -> None:
    check_equal(
        ConcretePipeline().process("payload"),
        (
            "ConcretePipeline",
            "PrefixStage",
            "LoggingStage",
            "PipelineStage",
            "payload",
        ),
        "Cooperative super() calls through abstract implementations failed",
    )
    check_equal(
        ConcretePipeline.__abstractmethods__,
        frozenset(),
        "Concrete diamond subclass must be instantiable",
    )


# -----------------------------------------------------------------------------
# 14. Python 3.12 PEP 695 generic ABCs
# -----------------------------------------------------------------------------


type Maybe[T] = T | None


class Repository[T](ABC):
    @abstractmethod
    def add(self, value: T) -> None:
        raise NotImplementedError

    @abstractmethod
    def get(self, index: int) -> Maybe[T]:
        raise NotImplementedError

    def first(self) -> Maybe[T]:
        return self.get(0)


class MemoryRepository[T](Repository[T]):
    def __init__(self) -> None:
        self._items: list[T] = []

    def add(self, value: T) -> None:
        self._items.append(value)

    def get(self, index: int) -> Maybe[T]:
        if 0 <= index < len(self._items):
            return self._items[index]
        return None


class ReadOnlyRepository[T](Repository[T]):
    def get(self, index: int) -> Maybe[T]:
        return None



def test_generic_abcs() -> None:
    check_equal(len(Repository.__type_params__), 1, "Repository must expose one type parameter")
    check_equal(
        Repository.__type_params__[0].__name__,
        "T",
        "Unexpected generic ABC type-parameter name",
    )
    check_equal(Maybe.__name__, "Maybe", "PEP 695 type alias name is incorrect")
    check_equal(len(Maybe.__type_params__), 1, "Generic type alias must expose its type parameter")

    expect_exception(TypeError, ReadOnlyRepository)

    repository = MemoryRepository[int]()
    repository.add(10)
    repository.add(20)

    check_equal(repository.first(), 10, "Generic ABC concrete method failed")
    check_equal(repository.get(1), 20, "Generic implementation failed")
    check_equal(repository.get(99), None, "Generic missing value must return None")
    check(isinstance(repository, Repository), "Parameterized implementation must satisfy ABC")


# -----------------------------------------------------------------------------
# 15. Bound type parameters combined with ABC
# -----------------------------------------------------------------------------


class Entity:
    def __init__(self, key: str) -> None:
        self.key = key


class EntityRepository[T: Entity](ABC):
    @abstractmethod
    def store(self, entity: T) -> T:
        raise NotImplementedError


class ConcreteEntityRepository[T: Entity](EntityRepository[T]):
    def store(self, entity: T) -> T:
        return entity



def test_bounded_generic_abc() -> None:
    parameter = EntityRepository.__type_params__[0]
    check_equal(parameter.__name__, "T", "Bound type parameter has an unexpected name")
    check(parameter.__bound__ is Entity, "PEP 695 type bound must resolve to Entity")

    repository = ConcreteEntityRepository[Entity]()
    entity = Entity("entity-1")
    check(repository.store(entity) is entity, "Bound generic ABC implementation failed")


# -----------------------------------------------------------------------------
# 16. Name mangling inside abstract interfaces
# -----------------------------------------------------------------------------


class SecureComponent(ABC):
    __category = "secure-component"

    @abstractmethod
    def __secret_operation(self) -> str:
        raise NotImplementedError

    def call_secret_operation(self) -> str:
        return self.__secret_operation()

    @classmethod
    def category(cls) -> str:
        return cls.__category


class IncorrectSecureComponent(SecureComponent):
    def __secret_operation(self) -> str:
        return "subclass-private-method"


class CorrectSecureComponent(SecureComponent):
    def _SecureComponent__secret_operation(self) -> str:
        return "implemented-base-mangled-name"



def test_abc_name_mangling() -> None:
    check(
        "_SecureComponent__secret_operation" in SecureComponent.__abstractmethods__,
        "Abstract private method must be stored under its mangled name",
    )
    check(
        "_SecureComponent__secret_operation" in SecureComponent.__dict__,
        "Private abstract method must be mangled in the class dictionary",
    )
    check(
        "_SecureComponent__category" in SecureComponent.__dict__,
        "Private class attribute must be mangled",
    )

    expect_exception(TypeError, IncorrectSecureComponent)

    correct = CorrectSecureComponent()
    check_equal(
        correct.call_secret_operation(),
        "implemented-base-mangled-name",
        "Implementing the actual mangled abstract name failed",
    )
    check_equal(
        CorrectSecureComponent.category(),
        "secure-component",
        "Mangled private class attribute lookup failed",
    )


# -----------------------------------------------------------------------------
# 17. Abstract descriptors
# -----------------------------------------------------------------------------


class AbstractField:
    """Descriptor that propagates abstractness from its getter function."""

    def __init__(self, getter) -> None:
        self.getter = getter

    @property
    def __isabstractmethod__(self) -> bool:
        return bool(getattr(self.getter, "__isabstractmethod__", False))

    def __get__(self, instance, owner=None):
        if instance is None:
            return self
        return self.getter(instance)


class DescriptorInterface(ABC):
    @AbstractField
    @abstractmethod
    def value(self) -> int:
        raise NotImplementedError


class DescriptorImplementation(DescriptorInterface):
    @property
    def value(self) -> int:
        return 123



def test_abstract_descriptors() -> None:
    check(
        DescriptorInterface.__dict__["value"].__isabstractmethod__,
        "Custom descriptor must expose __isabstractmethod__",
    )
    check(
        "value" in DescriptorInterface.__abstractmethods__,
        "ABCMeta must recognize an abstract custom descriptor",
    )
    expect_exception(TypeError, DescriptorInterface)
    check_equal(DescriptorImplementation().value, 123, "Concrete descriptor override failed")


# -----------------------------------------------------------------------------
# 18. Abstractness introspection
# -----------------------------------------------------------------------------


class IntrospectionInterface(ABC):
    @abstractmethod
    def first(self) -> None:
        raise NotImplementedError

    @abstractmethod
    def second(self) -> None:
        raise NotImplementedError

    def concrete(self) -> str:
        return "concrete"


class HalfImplementation(IntrospectionInterface):
    def first(self) -> None:
        pass


class FullImplementation(HalfImplementation):
    def second(self) -> None:
        pass



def test_abstractness_introspection() -> None:
    check_equal(
        IntrospectionInterface.__abstractmethods__,
        frozenset({"first", "second"}),
        "Base abstract-method introspection is incorrect",
    )
    check_equal(
        HalfImplementation.__abstractmethods__,
        frozenset({"second"}),
        "Partial implementation abstract-method introspection is incorrect",
    )
    check_equal(
        FullImplementation.__abstractmethods__,
        frozenset(),
        "Full implementation should expose no abstract methods",
    )

    check(
        IntrospectionInterface.first.__isabstractmethod__,
        "Abstract function must expose __isabstractmethod__",
    )
    check(
        not hasattr(IntrospectionInterface.concrete, "__isabstractmethod__"),
        "Ordinary concrete function should not be marked abstract",
    )
    check_equal(FullImplementation().concrete(), "concrete", "Concrete inherited method failed")


# -----------------------------------------------------------------------------
# 19. Virtual subclassing is independent from generic parameterization
# -----------------------------------------------------------------------------


class GenericReadable[T](ABC):
    @abstractmethod
    def read(self) -> T:
        raise NotImplementedError


class ExternalIntegerReader:
    def read(self) -> int:
        return 42



def test_generic_virtual_subclass() -> None:
    GenericReadable.register(ExternalIntegerReader)

    reader = ExternalIntegerReader()
    check(isinstance(reader, GenericReadable), "Generic ABC virtual registration failed")
    check_equal(reader.read(), 42, "Virtual generic implementation failed")

    # Parameterized generic aliases are intended for typing and cannot be used
    # as the second argument to isinstance().
    expect_exception(TypeError, lambda: isinstance(reader, GenericReadable[int]))


# -----------------------------------------------------------------------------
# 20. ABCs remain ordinary Python classes in all other respects
# -----------------------------------------------------------------------------


class StatefulInterface(ABC):
    class_attribute = "shared"

    def __init__(self, seed: int) -> None:
        self.seed = seed

    @abstractmethod
    def calculate(self, value: int) -> int:
        return self.seed + value

    def helper(self, value: int) -> int:
        return value * 2


class StatefulImplementation(StatefulInterface):
    def calculate(self, value: int) -> int:
        return super().calculate(value) * 3



def test_ordinary_class_features() -> None:
    instance = StatefulImplementation(5)
    check_equal(instance.seed, 5, "ABC constructor inheritance failed")
    check_equal(instance.helper(4), 8, "Concrete helper method on ABC failed")
    check_equal(instance.calculate(2), 21, "Abstract implementation with super() failed")
    check_equal(instance.class_attribute, "shared", "ABC class attribute lookup failed")


# -----------------------------------------------------------------------------
# Runner
# -----------------------------------------------------------------------------


TESTS = (
    ("basic ABC behavior", test_basic_abc_behavior),
    ("abstract method with reusable body", test_abstract_method_with_body),
    ("abstract properties", test_abstract_properties),
    ("abstract classmethod and staticmethod", test_abstract_class_and_static_methods),
    ("decorator order", test_decorator_order),
    ("direct ABCMeta usage", test_abcmeta_directly),
    ("virtual subclass registration", test_virtual_subclass_registration),
    ("register() as decorator", test_register_as_decorator),
    ("__subclasshook__ structural recognition", test_subclasshook),
    ("dynamic update_abstractmethods", test_update_abstractmethods),
    ("subclass refresh behavior", test_update_does_not_refresh_subclasses),
    ("multiple ABC inheritance", test_multiple_inheritance),
    ("cooperative abstract methods", test_cooperative_abstract_methods),
    ("PEP 695 generic ABC", test_generic_abcs),
    ("bounded generic ABC", test_bounded_generic_abc),
    ("ABC name mangling", test_abc_name_mangling),
    ("abstract custom descriptor", test_abstract_descriptors),
    ("abstractness introspection", test_abstractness_introspection),
    ("generic virtual subclass", test_generic_virtual_subclass),
    ("ordinary class features", test_ordinary_class_features),
)


def run_all_tests() -> None:
    print("Python 3.12+ Abstract Base Class test suite")
    print("=" * 50)

    for name, callback in TESTS:
        run_test(name, callback)

    passed = sum(success for _, success, _ in TEST_RESULTS)
    failed = len(TEST_RESULTS) - passed

    print("\n" + "=" * 50)
    print(f"Completed {len(TEST_RESULTS)} test groups: {passed} passed, {failed} failed.")

    if failed:
        print("\nFailures:")
        for name, success, detail in TEST_RESULTS:
            if not success:
                print(f"  {name}: {detail}")

        raise SystemExit(1)


run_all_tests()
