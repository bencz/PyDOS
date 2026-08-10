def demonstrate():
    outer_value = "visible"
    item = "outside"
    snapshots = [
        (item, locals().get("outer_value"), "item" in locals())
        for item in (10, 20)
    ]
    return item, snapshots


print(demonstrate())
