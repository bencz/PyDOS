def collect(*items, tag="none"):
    return (tag, len(items))


class Box:
    def __init__(self, *children, weights=()):
        self.children = children
        self.weights = weights


print(collect(1, 2, 3))
print(collect(4, tag="named"))
b = Box("a", "b", weights=(1, 3))
print(len(b.children), b.weights)
print(b.children[0], b.children[1])
c = Box()
print(len(c.children), len(c.weights))
