data = [[1, 2], [3, 4]]
column = 0

print(max(range(column, 2), key=lambda row: abs(data[row][column])))

column = 1
print(max(range(column, 2), key=lambda row: abs(data[row][column])))
print(max([2, 7, 4]))
print(min([2, 7, 4]))


def choose(rows, selected_column):
    return max(
        range(selected_column, 2),
        key=lambda row: abs(rows[row][selected_column])
    )


print(choose(data, 0))
