class DataDescriptor:
    def __init__(self):
        self.stored = 10

    def __get__(self, obj, owner):
        if obj is None:
            return "class-data"
        return self.stored

    def __set__(self, obj, value):
        self.stored = value * 2

    def __delete__(self, obj):
        self.stored = -1


class NonDataDescriptor:
    def __get__(self, obj, owner):
        if obj is None:
            return "class-nondata"
        return "descriptor"


class Owner:
    pass


Owner.data = DataDescriptor()
Owner.nondata = NonDataDescriptor()

item = Owner()
print(item.data)
item.data = 7
print(item.data)
vars(item)["data"] = 99
print(item.data)
del item.data
print(item.data)
print(Owner.data)

print(item.nondata)
item.nondata = "instance"
print(item.nondata)
print(Owner.nondata)
