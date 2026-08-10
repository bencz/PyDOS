class CallMatrix:
    def zero(self):
        return "zero"

    def one(self, value):
        return value

    def four(self, a, b, c, d):
        return a * 1000 + b * 100 + c * 10 + d

    def defaults(self, a, b=10, c=100):
        return a + b + c

    def many(self, head, *rest):
        return head + len(rest)

    def seven(self, a, b, c, d, e, f, g):
        return a + b + c + d + e + f + g

    def keyword_only(self, left, *, right=50):
        return left + right

    def options(self, base=0, **values):
        return base + len(values)


item = CallMatrix()
print(item.zero())
print(item.one(7))
print(item.four(1, 2, 3, 4))
print(item.four(d=4, b=2, a=1, c=3))
print(item.defaults(1))
print(item.defaults(1, 2))
print(item.defaults(1, 2, 3))
print(item.defaults(1, c=3))
print(item.defaults(c=3, a=1, b=2))
dynamic_defaults = getattr(item, "defaults")
print(dynamic_defaults(1))
print(dynamic_defaults(1, 2))
print(item.many(10))
print(item.many(10, 20, 30, 40))
print(item.seven(1, 2, 3, 4, 5, 6, 7))
print(item.keyword_only(2))
print(item.keyword_only(2, right=8))
print(item.options())
print(item.options(10, alpha=1, beta=2, gamma=3))


def class_four(self, a, b, c, d):
    return a + b + c + d


CallMatrix.four = class_four
print(item.four(1, 2, 3, 4))


def instance_four(a, b, c, d):
    return a * b * c * d


item.four = instance_four
print(item.four(1, 2, 3, 4))

try:
    item.one()
    print("missing not rejected")
except TypeError:
    print("missing rejected")

try:
    item.one(1, 2)
    print("extra not rejected")
except TypeError:
    print("extra rejected")

try:
    item.zero(1)
    print("zero extra not rejected")
except TypeError:
    print("zero extra rejected")

try:
    item.seven(1, 2, 3)
    print("many missing not rejected")
except TypeError:
    print("many missing rejected")


def class_pair(self, left, right):
    return left * 10 + right


del item.four
CallMatrix.four = class_pair
print(item.four(6, 7))

try:
    item.four(6)
    print("replacement missing not rejected")
except TypeError:
    print("replacement missing rejected")

try:
    item.four(6, 7, 8)
    print("replacement extra not rejected")
except TypeError:
    print("replacement extra rejected")


def instance_unary(value):
    return value + 100


item.four = instance_unary
print(item.four(5))

try:
    item.four()
    print("instance missing not rejected")
except TypeError:
    print("instance missing rejected")

try:
    item.four(1, 2)
    print("instance extra not rejected")
except TypeError:
    print("instance extra rejected")
