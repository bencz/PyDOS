from pydos.io.tui import Canvas, Color, Key, Screen, delay, ticks_ms


canvas = Canvas(16, 6, ".")
canvas.draw_box(0, 0, 16, 6, "PyDOS")
canvas.center(2, "TUI")
canvas.draw_text(-2, 3, "ABCDE")
canvas.draw_text(13, 4, "WXYZ")
for line in canvas.to_lines():
    print(line)

print(Color.YELLOW)
print(Key.LEFT)

screen = Screen()
print(screen.width)
print(screen.height)

start = ticks_ms()
delay(0)
finish = ticks_ms()
print(start >= 0)
print(finish >= 0)
