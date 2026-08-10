# gen_fold.py - identical generic methods share code without losing vtables

class Box[T]:
    def __init__(self, value: T) -> None:
        self.value: T = value

    def get(self) -> T:
        return self.value

    def combine(self, other: T) -> T:
        return self.value + other

def dynamic_get(obj):
    return obj.get()

def main() -> None:
    numbers: Box[int] = Box[int](40)
    words: Box[str] = Box[str]("Py")

    # Statically known calls may be devirtualized.
    print(numbers.get())
    print(words.get())

    # Any-typed calls must still use each specialization's vtable, even when
    # both entries point to one shared implementation body.
    print(dynamic_get(numbers))
    print(dynamic_get(words))

    # Reflection continues to see the method in both vtables.
    print(hasattr(numbers, "get"))
    print(hasattr(words, "get"))

    # Type-sensitive bodies must not be folded together.
    print(numbers.combine(2))
    print(words.combine("DOS"))

main()
