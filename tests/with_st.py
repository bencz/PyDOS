# Test with statement
class CtxMgr:
    name: str
    def __init__(self, name: str) -> None:
        self.name = name

    def __enter__(self) -> str:
        print("enter " + self.name)
        return self.name

    def __exit__(self, exc_type, exc_val, tb) -> bool:
        print("exit " + self.name)
        return False

# Basic with
with CtxMgr("A") as val:
    print("body " + val)

# Nested with (two managers)
with CtxMgr("X") as x:
    with CtxMgr("Y") as y:
        print("inner " + x + " " + y)
