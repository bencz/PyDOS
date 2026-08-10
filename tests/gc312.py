from gc import collect, get_threshold, is_tracked, set_threshold


print(is_tracked([]))
print(is_tracked(42))
print(collect() >= 0)

cycle = []
cycle.append(cycle)
del cycle
print(collect() >= 1)

print(collect(0) >= 0)
print(collect(1) >= 0)
print(collect(2) >= 0)

original = get_threshold()
set_threshold(25, 4, 5)
print(get_threshold() == (25, 4, 5))
set_threshold(original[0], original[1], original[2])
