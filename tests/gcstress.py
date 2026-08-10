from gc import collect, disable, enable, get_count, get_stats
from gc import get_threshold, isenabled, is_tracked, set_threshold


ORIGINAL_THRESHOLD = get_threshold()


class Node:
    def __init__(self, name):
        self.name = name
        self.peer = None


def make_self_list_cycle():
    item = []
    item.append(item)


def make_mutual_list_cycle():
    left = []
    right = []
    left.append(right)
    right.append(left)


def make_dict_cycle():
    mapping = {}
    mapping["self"] = mapping


def make_mixed_cycle():
    values = []
    mapping = {"values": values}
    values.append(mapping)


def container_cycles():
    collect()

    make_self_list_cycle()
    self_ok = collect() >= 1

    make_mutual_list_cycle()
    mutual_ok = collect() >= 2

    make_dict_cycle()
    dict_ok = collect() >= 1

    make_mixed_cycle()
    mixed_ok = collect() >= 2

    print("containers", self_ok, mutual_ok, dict_ok, mixed_ok)


def use_reachable_cycle():
    collect()
    root = []
    child = {"root": root}
    root.append(child)

    collect()
    alive = len(root) == 1 and child["root"] is root
    return alive


def reachable_cycle():
    alive = use_reachable_cycle()
    released = collect() >= 2
    print("reachable", alive, released)


def use_instance_cycle():
    collect()
    first = Node("first")
    second = Node("second")
    first.peer = second
    second.peer = first
    return first.peer.name == "second" and second.peer.name == "first"


def instance_cycle():
    alive = use_instance_cycle()
    released = collect() >= 2
    print("instances", alive, released)


def make_closure_cycle():
    holder = []

    def callback():
        return len(holder)

    holder.append(callback)
    return holder


def closure_cycle():
    collect()
    root = make_closure_cycle()
    alive = root[0]() == 1
    return alive


def test_closure_cycle():
    alive = closure_cycle()
    released = collect() >= 3
    print("closure", alive, released)


def make_deep_cycle(depth):
    root = []
    current = root
    index = 1
    while index < depth:
        child = []
        current.append(child)
        current = child
        index += 1
    current.append(root)
    return root


def use_deep_cycle():
    collect()
    root = make_deep_cycle(64)
    current = root
    depth = 1
    while depth < 64:
        current = current[0]
        depth += 1
    alive = current[0] is root
    return alive


def deep_cycle():
    alive = use_deep_cycle()
    released = collect() >= 64
    print("deep", alive, released)


def cyclic_generator(holder):
    yield len(holder)


def use_generator_cycle():
    collect()
    holder = []
    generator = cyclic_generator(holder)
    holder.append(generator)
    alive = next(generator) == 1
    return alive


def generator_cycle():
    alive = use_generator_cycle()
    released = collect() >= 0
    print("generator", alive, released)


def make_pressure_cycle():
    item = []
    item.append(item)


def automatic_collection():
    original = get_threshold()
    collect()
    disable()
    before = get_stats()[0]["collections"]

    index = 0
    while index < 80:
        make_pressure_cycle()
        index += 1

    disabled_ok = get_stats()[0]["collections"] == before
    count_ok = get_count()[0] >= 80
    manual_ok = collect() >= 80

    set_threshold(20, original[1], original[2])
    enable()
    before = get_stats()[0]["collections"]

    index = 0
    while index < 80:
        make_pressure_cycle()
        index += 1

    automatic_ok = get_stats()[0]["collections"] > before
    collect()
    set_threshold(original[0], original[1], original[2])
    print("automatic", disabled_ok, count_ok, manual_ok, automatic_ok)


print("state", isenabled(), len(ORIGINAL_THRESHOLD) == 3)
print("tracked", is_tracked([]), is_tracked({"items": []}), is_tracked(1))
container_cycles()
reachable_cycle()
instance_cycle()
test_closure_cycle()
deep_cycle()
generator_cycle()
automatic_collection()
print("done", isenabled(), get_threshold() == ORIGINAL_THRESHOLD)
