def descend(depth):
    marker = "(" + str(depth) + ")"
    if depth == 0:
        return len(marker)
    return descend(depth - 1) + len(marker)


iteration = 0
total = 0
while iteration < 40:
    total += descend(24)
    iteration += 1

print(descend(24))
print(total)
print("arena scopes balanced")
