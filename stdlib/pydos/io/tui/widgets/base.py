"""Base widget protocol."""


class Widget:
    def __init__(self, x, y, width, height=1):
        self.x = x
        self.y = y
        self.width = width
        self.height = height
        self.visible = True
        self.enabled = True

    def draw(self, canvas, focused=False):
        pass

    def handle_key(self, key):
        return False
