"""Python-level dataclass support for PyDOS.

The physical filename is the deterministic DOS 8.3 alias for the logical
module name ``dataclasses``.

The implementation deliberately uses the normal class/decorator/reflection
protocol.  The C runtime only supplies object-model primitives; field policy
and generated behavior remain here.
"""


class _MissingType:
    def __repr__(self):
        return "<dataclasses.MISSING>"


MISSING = _MissingType()


class _FieldSpec:
    pass


class Field:
    pass


class _DataclassParams:
    def __init__(self, init, repr_value, eq, order, unsafe_hash, frozen):
        self.init = init
        self.repr = repr_value
        self.eq = eq
        self.order = order
        self.unsafe_hash = unsafe_hash
        self.frozen = frozen


def field(default=MISSING, default_factory=MISSING, init=True, repr=True,
          hash=None, compare=True, metadata=None, kw_only=False):
    if default is not MISSING and default_factory is not MISSING:
        raise ValueError("cannot specify both default and default_factory")
    if metadata is None:
        metadata = {}
    result = _FieldSpec()
    result.default = default
    result.default_factory = default_factory
    result.init = init
    result.repr = repr
    result.hash = hash
    result.compare = compare
    result.metadata = metadata
    result.kw_only = kw_only
    return result


def _find_field_index(result, name):
    index = 0
    while index < len(result):
        if result[index].name == name:
            return index
        index += 1
    return -1


def _collect_fields(cls):
    result = []
    mro = cls.__mro__
    mro_index = len(mro) - 1
    while mro_index >= 0:
        base = mro[mro_index]
        if base is not cls:
            namespace = base.__dict__
            if "__dataclass_fields__" in namespace:
                for inherited in namespace["__dataclass_fields__"]:
                    result.append(inherited)
        mro_index -= 1

    namespace = cls.__dict__
    annotations = namespace.get("__annotations__", {})
    for item in annotations.items():
        name = item[0]
        field_type = item[1]
        default = MISSING
        default_factory = MISSING
        init_value = True
        repr_value = True
        hash_value = None
        compare_value = True
        metadata_value = {}
        kw_only_value = False

        if name in namespace:
            raw_default = namespace[name]
            if isinstance(raw_default, _FieldSpec):
                default = raw_default.default
                default_factory = raw_default.default_factory
                init_value = raw_default.init
                repr_value = raw_default.repr
                hash_value = raw_default.hash
                compare_value = raw_default.compare
                metadata_value = raw_default.metadata
                kw_only_value = raw_default.kw_only
                if default is not MISSING:
                    setattr(cls, name, default)
                else:
                    delattr(cls, name)
            else:
                default = raw_default

        new_field = Field()
        new_field.name = name
        new_field.type = field_type
        new_field.default = default
        new_field.default_factory = default_factory
        new_field.init = init_value
        new_field.repr = repr_value
        new_field.hash = hash_value
        new_field.compare = compare_value
        new_field.metadata = metadata_value
        new_field.kw_only = kw_only_value
        existing = _find_field_index(result, name)
        if existing >= 0:
            result[existing] = new_field
        else:
            result.append(new_field)
    return result


def _field_value(field_value):
    if field_value.default_factory is not MISSING:
        return field_value.default_factory()
    if field_value.default is not MISSING:
        return field_value.default
    return MISSING


def _generated_init(self, value0=MISSING, value1=MISSING,
                    value2=MISSING, value3=MISSING, value4=MISSING,
                    value5=MISSING, value6=MISSING):
    supplied = [value0, value1, value2, value3, value4, value5, value6]
    supplied_index = 0
    for field_value in type(self).__dataclass_fields__:
        if field_value.init:
            value = supplied[supplied_index]
            supplied_index += 1
            if value is MISSING:
                value = _field_value(field_value)
            if value is MISSING:
                raise TypeError("missing required dataclass field: "
                                + field_value.name)
            setattr(self, field_value.name, value)
        elif field_value.default_factory is not MISSING:
            setattr(self, field_value.name,
                    field_value.default_factory())

    post_init = getattr(self, "__post_init__", None)
    if post_init is not None:
        post_init()


def _generated_repr(self):
    parts = []
    for field_value in type(self).__dataclass_fields__:
        if field_value.repr:
            parts.append(field_value.name + "="
                         + repr(getattr(self, field_value.name)))
    return type(self).__name__ + "(" + ", ".join(parts) + ")"


def _generated_eq(self, other):
    if type(self) is not type(other):
        return NotImplemented
    for field_value in type(self).__dataclass_fields__:
        if field_value.compare:
            if getattr(self, field_value.name) != getattr(
                    other, field_value.name):
                return False
    return True


def _generated_lt(self, other):
    if type(self) is not type(other):
        return NotImplemented
    for field_value in type(self).__dataclass_fields__:
        if field_value.compare:
            left = getattr(self, field_value.name)
            right = getattr(other, field_value.name)
            if left < right:
                return True
            if left > right:
                return False
    return False


def _generated_le(self, other):
    return self == other or self < other


def _generated_gt(self, other):
    if type(self) is not type(other):
        return NotImplemented
    return not (self <= other)


def _generated_ge(self, other):
    if type(self) is not type(other):
        return NotImplemented
    return not (self < other)


def _process_class(cls, init, repr_value, eq, order, unsafe_hash, frozen):
    if frozen:
        raise NotImplementedError(
            "frozen dataclasses require raw attribute assignment support")

    result = _collect_fields(cls)
    init_count = 0
    seen_default = False
    match_args = []
    for field_value in result:
        if field_value.init:
            init_count += 1
            if not field_value.kw_only:
                match_args.append(field_value.name)
            has_default = (field_value.default is not MISSING or
                           field_value.default_factory is not MISSING)
            if has_default:
                seen_default = True
            elif seen_default:
                raise TypeError("non-default field follows default field")
    if init_count > 7:
        raise TypeError("PyDOS dataclasses support at most 7 init fields")

    setattr(cls, "__dataclass_fields__", result)
    setattr(cls, "__dataclass_params__",
            _DataclassParams(init, repr_value, eq, order,
                             unsafe_hash, frozen))
    setattr(cls, "__match_args__", tuple(match_args))
    namespace = cls.__dict__
    if init and "__init__" not in namespace:
        setattr(cls, "__init__", _generated_init)
    if repr_value and "__repr__" not in namespace:
        setattr(cls, "__repr__", _generated_repr)
    if eq and "__eq__" not in namespace:
        setattr(cls, "__eq__", _generated_eq)
    if order:
        if not eq:
            raise ValueError("eq must be true if order is true")
        if "__lt__" not in namespace:
            setattr(cls, "__lt__", _generated_lt)
        if "__le__" not in namespace:
            setattr(cls, "__le__", _generated_le)
        if "__gt__" not in namespace:
            setattr(cls, "__gt__", _generated_gt)
        if "__ge__" not in namespace:
            setattr(cls, "__ge__", _generated_ge)
    return cls


def dataclass(cls=None, init=True, repr=True, eq=True, order=False,
              unsafe_hash=False, frozen=False, **options):
    if options.get("slots", False):
        raise NotImplementedError("slots dataclasses are not supported")
    if cls is None:
        def wrap(target):
            return _process_class(target, init, repr, eq, order,
                                  unsafe_hash, frozen)
        return wrap
    return _process_class(cls, init, repr, eq, order, unsafe_hash, frozen)


def fields(class_or_instance):
    if isinstance(class_or_instance, type):
        cls = class_or_instance
    else:
        cls = type(class_or_instance)
    if not hasattr(cls, "__dataclass_fields__"):
        raise TypeError("must be called with a dataclass type or instance")
    return tuple(cls.__dataclass_fields__)


def is_dataclass(obj):
    if isinstance(obj, type):
        cls = obj
    else:
        cls = type(obj)
    return hasattr(cls, "__dataclass_fields__")


def _asdict_inner(obj):
    if is_dataclass(obj) and not isinstance(obj, type):
        dataclass_result = {}
        for field_value in fields(obj):
            dataclass_result[field_value.name] = _asdict_inner(
                getattr(obj, field_value.name))
        return dataclass_result
    if isinstance(obj, list):
        list_result = []
        for item in obj:
            list_result.append(_asdict_inner(item))
        return list_result
    if isinstance(obj, tuple):
        tuple_result = []
        for item in obj:
            tuple_result.append(_asdict_inner(item))
        return tuple(tuple_result)
    if isinstance(obj, dict):
        dict_result = {}
        for item in obj.items():
            dict_result[_asdict_inner(item[0])] = _asdict_inner(item[1])
        return dict_result
    return obj


def asdict(obj):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("asdict() should be called on dataclass instances")
    return _asdict_inner(obj)


def astuple(obj):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("astuple() should be called on dataclass instances")
    result = []
    for field_value in fields(obj):
        result.append(_asdict_inner(getattr(obj, field_value.name)))
    return tuple(result)


def replace(obj, **changes):
    if not is_dataclass(obj) or isinstance(obj, type):
        raise TypeError("replace() should be called on dataclass instances")
    values = []
    for field_value in fields(obj):
        if field_value.init:
            values.append(changes.get(
                field_value.name, getattr(obj, field_value.name)))
    cls = type(obj)
    count = len(values)
    if count == 0:
        return cls()
    if count == 1:
        return cls(values[0])
    if count == 2:
        return cls(values[0], values[1])
    if count == 3:
        return cls(values[0], values[1], values[2])
    if count == 4:
        return cls(values[0], values[1], values[2], values[3])
    if count == 5:
        return cls(values[0], values[1], values[2], values[3], values[4])
    if count == 6:
        return cls(values[0], values[1], values[2], values[3], values[4],
                   values[5])
    return cls(values[0], values[1], values[2], values[3], values[4],
               values[5], values[6])
