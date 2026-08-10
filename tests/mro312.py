class A:
    def who(self):
        return "A"

    def combine(self, left, right):
        return left - right


class B(A):
    pass


class C(A):
    def who(self):
        return "C"

    def combine(self, left, right):
        return left + right


class D(B, C):
    pass


A.label = "A-label"
C.label = "C-label"

item = D()
print(item.who())
print(item.label)
print(item.combine(2, 3))
print(len(D.__mro__))
print(D.__mro__[0].__name__)
print(D.__mro__[1].__name__)
print(D.__mro__[2].__name__)
print(D.__mro__[3].__name__)
print(D.__mro__[4].__name__)
print(issubclass(D, A))
print(isinstance(item, C))


def replacement(self):
    return "dynamic"


D.who = replacement
print(item.who())
del D.who
print(item.who())


def instance_replacement(prefix, suffix):
    return prefix + suffix


item.who = instance_replacement
print(item.who("inst", "ance"))


def class_combine_replacement(self, left, right):
    return left * right


D.combine = class_combine_replacement
print(item.combine(2, 3))
del D.combine
print(item.combine(2, 3))


def instance_combine_replacement(left, right):
    return left - right


item.combine = instance_combine_replacement
print(item.combine(9, 4))
