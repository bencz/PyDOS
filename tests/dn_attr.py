# dn_attr.py - Custom attribute access dunder
class Proxy:
    def __init__(self) -> None:
        self.data = "hello"
    def __getattr__(self, name: str) -> str:
        return "proxy:" + name

p: Proxy = Proxy()
print(p.data)
print(p.missing)
print(p.anything)
