"""Memory-backed text canvas and clipping operations."""


class Canvas:
    def __init__(self, width=80, height=25, fill=" "):
        self.width = width
        self.height = height
        self.rows = []
        self.clear(fill)

    def clear(self, fill=" "):
        if len(fill) == 0:
            fill = " "
        line = fill[0] * self.width
        self.rows = []
        y = 0
        while y < self.height:
            self.rows.append(line)
            y += 1

    def draw_text(self, x, y, text):
        if y < 0 or y >= self.height:
            return
        value = str(text)
        if x < 0:
            skip = -x
            if skip >= len(value):
                return
            value = value[skip:]
            x = 0
        if x >= self.width or len(value) == 0:
            return
        available = self.width - x
        if len(value) > available:
            value = value[:available]
        row = self.rows[y]
        end = x + len(value)
        self.rows[y] = row[:x] + value + row[end:]

    def draw_hline(self, x, y, length, char="-"):
        if length > 0:
            self.draw_text(x, y, char[0] * length)

    def draw_vline(self, x, y, length, char="|"):
        offset = 0
        while offset < length:
            self.draw_text(x, y + offset, char[0])
            offset += 1

    def draw_box(self, x, y, width, height, title=""):
        if width < 2 or height < 2:
            return
        self.draw_text(x, y, "+" + "-" * (width - 2) + "+")
        self.draw_text(x, y + height - 1,
                       "+" + "-" * (width - 2) + "+")
        self.draw_vline(x, y + 1, height - 2, "|")
        self.draw_vline(x + width - 1, y + 1, height - 2, "|")
        if len(title) > 0 and width > 4:
            shown = " " + title + " "
            if len(shown) > width - 2:
                shown = shown[:width - 2]
            self.draw_text(x + 2, y, shown)

    def center(self, y, text):
        value = str(text)
        self.draw_text((self.width - len(value)) // 2, y, value)

    def get_line(self, y):
        if y < 0 or y >= self.height:
            return ""
        return self.rows[y]

    def to_lines(self):
        return self.rows.copy()
