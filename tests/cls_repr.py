class Pessoa:
    pass

p: Pessoa = Pessoa()
representation: str = repr(p)
print(representation.startswith("<__main__.Pessoa object at 0x"))
print(str(p) == representation)
