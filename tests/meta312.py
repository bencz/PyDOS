events = []


class TrackingMeta(type):
    @classmethod
    def __prepare__(mcls, name, bases, **kwargs):
        events.append("prepare:" + kwargs["tag"])
        return {"prepared_value": kwargs["tag"]}

    def __new__(mcls, name, bases, namespace, **kwargs):
        events.append("new:" + str("method" in namespace))
        namespace["created_value"] = kwargs["tag"] + ":new"
        return type.__new__(mcls, name, bases, namespace)

    def __init__(cls, name, bases, namespace, **kwargs):
        events.append("init:" + kwargs["tag"])


class Tracked(metaclass=TrackingMeta, tag="ok"):
    marker = 7

    def method(self):
        return self.marker


print(events)
print(type(Tracked) is TrackingMeta)
print(Tracked.prepared_value)
print(Tracked.created_value)
print(Tracked.marker)
print(Tracked().method())


class TrackedChild(Tracked, tag="child"):
    child_marker = 9

    def method(self):
        return self.child_marker


print(events)
print(type(TrackedChild) is TrackingMeta)
print(TrackedChild.prepared_value)
print(TrackedChild.created_value)
print(TrackedChild.child_marker)
print(TrackedChild().method())


class RecordingNamespace:
    def __init__(self):
        self.data = {}

    def __setitem__(self, key, value):
        self.data[key] = value

    def __getitem__(self, key):
        return self.data[key]

    def keys(self):
        return self.data.keys()


class MappingMeta(type):
    @classmethod
    def __prepare__(mcls, name, bases, **kwargs):
        return RecordingNamespace()

    def __new__(mcls, name, bases, namespace, **kwargs):
        namespace["from_mapping"] = kwargs["label"]
        return type.__new__(mcls, name, bases, namespace.data)


class MappingTracked(metaclass=MappingMeta, label="custom"):
    value = 11

    def get_value(self):
        return self.value


print(type(MappingTracked) is MappingMeta)
print(MappingTracked.from_mapping)
print(MappingTracked.value)
print(MappingTracked().get_value())
