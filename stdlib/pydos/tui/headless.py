"""Screen double for tests: same surface as Screen, zero C calls.

``present`` keeps a copy of the last frame in ``self.buffer``, so a
scripted application run ends with the final picture available to
print against a golden file:

    app = Editor(screen=HeadlessScreen(80, 25),
                 input=ScriptedInput(["ctrl+o", "a", "enter", "escape"]))
    app.run()
    print(app.screen.buffer)
"""

from pydos.tui.buffer import Buffer


class HeadlessScreen:
    def __init__(self, width: int = 80, height: int = 25) -> None:
        self.width = width
        self.height = height
        self.mono = False
        self.buffer = Buffer(width, height)
        self.presents = 0
        self.last_cursor = (-1, -1, 0)

    def present(self, buffer: Buffer, x: int = 0, y: int = 0) -> None:
        self.buffer.blit(buffer, x, y)
        self.presents += 1

    def cursor(self, x: int, y: int, shape: int = 1) -> None:
        self.last_cursor = (x, y, shape)

    def hide_cursor(self) -> None:
        self.last_cursor = (-1, -1, 0)

    def set_rows(self, rows: int) -> int:
        return self.height

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        return False
