class First:
    class Inner:
        __value = "first"

        def reveal(self):
            return self.__value


class Second:
    class Inner:
        __value = "second"

        def reveal(self):
            return self.__value


print(First.Inner.__name__)
print(First.Inner().reveal())
print(Second.Inner().reveal())
print("_Inner__value" in vars(First.Inner))
print(First.Inner is Second.Inner)
