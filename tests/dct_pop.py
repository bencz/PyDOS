d: dict = {"x": 10, "y": 20, "z": 30}

v: object = d.pop("y")
print(v)
print(len(d))

sd: object = d.setdefault("w", 99)
print(sd)
print(d["w"])

sd2: object = d.setdefault("x", 999)
print(sd2)
