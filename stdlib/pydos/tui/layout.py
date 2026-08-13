"""Linear containers: VBox stacks children, HBox lines them up.

Sizing per child, in order of precedence:

    size_hint > 0   fixed number of rows (VBox) / columns (HBox)
    weights         proportional share of the remaining space, taken
                    from the ``weights`` tuple by child position (or
                    the child's own ``weight``, default 1)

The last flexible child absorbs the division remainder, so the children
always tile the container exactly.  Invisible children get a zero rect.

    root = VBox(
        MenuBar(menus),
        HBox(FileList(), TextArea(), weights=(1, 3)),
        StatusBar(),
    )
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget


class _LinearBox(Widget):
    def __init__(self) -> None:
        super().__init__()
        self.weights = ()

    def _weight_of(self, index: int) -> int:
        if index < len(self.weights):
            return self.weights[index]
        return self.children[index].weight

    def _measure(self, axis_size: int) -> list:
        """Size along the main axis for every child (0 when invisible)."""
        sizes: list = []
        fixed_total: int = 0
        weight_total: int = 0
        last_flexible: int = -1
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            if not child.visible:
                sizes.append(0)
            elif child.size_hint > 0:
                sizes.append(child.size_hint)
                fixed_total += child.size_hint
            else:
                sizes.append(-1)
                weight_total += self._weight_of(i)
                last_flexible = i
            i += 1

        remaining: int = axis_size - fixed_total
        if remaining < 0:
            remaining = 0
        used: int = 0
        i = 0
        while i < len(self.children):
            if sizes[i] < 0:
                if i == last_flexible:
                    share: int = remaining - used
                else:
                    share = remaining * self._weight_of(i) // weight_total
                if share < 0:
                    share = 0
                sizes[i] = share
                used += share
            i += 1
        return sizes


class VBox(_LinearBox):
    def __init__(self, *children, weights=()):
        super().__init__()
        self.weights = weights
        for child in children:
            self.add(child)

    def layout(self) -> None:
        sizes: list = self._measure(self.rect.height)
        y: int = self.rect.y
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            child.rect = Rect(self.rect.x, y, self.rect.width, sizes[i])
            child.layout()
            y += sizes[i]
            i += 1


class HBox(_LinearBox):
    def __init__(self, *children, weights=()):
        super().__init__()
        self.weights = weights
        for child in children:
            self.add(child)

    def layout(self) -> None:
        sizes: list = self._measure(self.rect.width)
        x: int = self.rect.x
        i: int = 0
        while i < len(self.children):
            child = self.children[i]
            child.rect = Rect(x, self.rect.y, sizes[i], self.rect.height)
            child.layout()
            x += sizes[i]
            i += 1
