class FeatureCollection[T]:
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
    def from_iterable[U](cls, items, label: str = "generated"):
        return cls(list(items), label)

    @property
    def label(self) -> str:
        return self.__label

    @label.setter
    def label(self, value: str) -> None:
        self.__label = self.normalize_label(value)

    def __iter__(self):
        return iter(self._items)

    def __len__(self) -> int:
        return len(self._items)

    def __contains__(self, item: object) -> bool:
        return item in self._items

    def __getitem__(self, index: int | slice):
        return self._items[index]

    def __call__[U](self, transform):
        return tuple(transform(item) for item in self._items)


collection = FeatureCollection([1, 2, 3], "  sample   values ")
print(collection[0])
print(collection[1:])
print(collection[0] == 1)
print(collection[1:] == [2, 3])
