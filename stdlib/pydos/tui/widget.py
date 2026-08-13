"""Base class of the widget tree.

A widget owns a rectangle, optional children, and five hooks:

    on_mount()            once, after the tree is wired to an App
    layout()              assign children's rects from self.rect
    render_self(buffer)   draw this widget
    on_key(event)         True when the key was consumed
    on_mouse(event)       True when the click was consumed

State that changes the picture goes through invalidate(): the App
recomposes the tree into its Buffer and the C engine writes only the
cells that differ — there is no per-widget damage tracking to get wrong.
"""

from pydos.tui.geometry import Rect
from pydos.tui.color import Style
from pydos.tui.buffer import Buffer
from pydos.tui.theme import Theme


class Widget:
    def __init__(self) -> None:
        self.rect = Rect(0, 0, 0, 0)
        self.visible = True
        self.enabled = True
        self.focusable = False
        self.focused = False
        self.parent = None
        self.children = []
        self.app = None
        self.theme = None
        self.weight = 1
        self.size_hint = 0

    def add(self, child):
        """Append a child (returns it, so build() code can keep a ref)."""
        child.parent = self
        self.children.append(child)
        return child

    def mount(self, app, theme: Theme) -> None:
        """Wire the subtree to an app and theme, firing on_mount DFS."""
        self.app = app
        self.theme = theme
        self.on_mount()
        i: int = 0
        while i < len(self.children):
            self.children[i].mount(app, theme)
            i += 1

    def on_mount(self) -> None:
        pass

    def layout(self) -> None:
        """Default: every child covers this widget's whole rect."""
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            child.rect = Rect(self.rect.x, self.rect.y,
                              self.rect.width, self.rect.height)
            child.layout()
            i += 1

    def render(self, buffer: Buffer) -> None:
        if not self.visible:
            return
        self.render_self(buffer)
        i: int = 0
        while i < len(self.children):
            self.children[i].render(buffer)
            i += 1

    def render_self(self, buffer: Buffer) -> None:
        pass

    def on_key(self, event) -> bool:
        return False

    def on_mouse(self, event) -> bool:
        return False

    def cursor_pos(self) -> tuple:
        """(x, y, shape) for the hardware cursor; (-1, -1, 0) hides it."""
        return (-1, -1, 0)

    def invalidate(self) -> None:
        if self.app is not None:
            self.app.invalidate()

    def theme_style(self, name: str) -> Style:
        if self.theme is not None:
            return self.theme.style(name)
        return Style()

    def hit_test(self, x: int, y: int):
        """Deepest visible descendant containing the point, else None."""
        if not self.visible or not self.rect.contains(x, y):
            return None
        i: int = len(self.children) - 1
        while i >= 0:
            found = self.children[i].hit_test(x, y)
            if found is not None:
                return found
            i -= 1
        return self
