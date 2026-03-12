from mod_hlp import add, greet, Counter

def run() -> None:
    print(add(3, 4))
    print(greet("DOS"))
    c: Counter = Counter(0)
    c.inc()
    c.inc()
    c.inc()
    print(c.get())

run()
