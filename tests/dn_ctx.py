# dn_ctx.py - Context manager dunder dispatch
# Tests: __enter__, __exit__ via vtable inheritance
class Base:
    def __enter__(self) -> str:
        print("base enter")
        return "base"
    def __exit__(self, et, ev, tb) -> bool:
        print("base exit")
        return False

class Child(Base):
    def __enter__(self) -> str:
        print("child enter")
        return "child"

# Child inherits __exit__ from Base
with Child() as c:
    print("body " + c)

# Plain base
with Base() as b:
    print("body " + b)
