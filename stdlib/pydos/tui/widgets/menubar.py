"""Menu bar with dropdown menus, declared as data.

    MENUS = [
        ("File", [("New", "Ctrl+N", "new"),
                  ("-", "", ""),
                  ("Exit", "Ctrl+Q", "quit_app")]),
        ("View", [("Line numbers", "", "line_numbers")]),
    ]
    bar = MenuBar(MENUS)

Selecting an item runs ``app.action(action)``.  F10 opens the first
menu and Alt+first-letter opens each one directly — the hotkeys are
registered on mount, and every dropdown position is derived from the
same data that renders the titles, so nothing can drift out of sync.
Toggle items show a check mark via ``set_checked(action, True)``.
"""

from pydos.tui.geometry import Rect
from pydos.tui.widget import Widget
from pydos.tui.buffer import Buffer
from pydos.tui.glyphs import Border
from pydos.tui.keys import Key
from pydos.tui.events import EventType


class _MenuPopup(Widget):
    def __init__(self, bar) -> None:
        super().__init__()
        self.bar = bar

    def on_mouse(self, event) -> bool:
        if event.kind == EventType.MOUSE_UP:
            index: int = event.y - self.rect.y - 1
            items: list = self.bar.current_items()
            if 0 <= index < len(items) and items[index][0] != "-":
                self.bar.item_index = index
                self.bar.activate_current()
            return True
        return event.kind == EventType.MOUSE_DOWN

    def render_self(self, buffer: Buffer) -> None:
        bar = self.bar
        style = self.theme_style("menu")
        chosen = self.theme_style("menu.selected")
        shortcut_style = self.theme_style("menu.shortcut")
        buffer.fill(self.rect, " ", style)
        buffer.box(self.rect, style, "", bar.popup_border)
        items: list = bar.current_items()
        i: int = 0
        while i < len(items):
            y: int = self.rect.y + 1 + i
            label: str = items[i][0]
            if label == "-":
                buffer.hline(self.rect.x + 1, y, self.rect.width - 2,
                             bar.popup_border[4], style)
            else:
                line_style = chosen if i == bar.item_index else style
                width: int = self.rect.width - 2
                mark: str = "*" if bar.is_checked(items[i][2]) else " "
                text: str = mark + label
                if len(text) > width:
                    text = text[:width]
                else:
                    text = text + " " * (width - len(text))
                buffer.text(self.rect.x + 1, y, text, line_style)
                shortcut: str = items[i][1]
                if len(shortcut) > 0 and len(shortcut) + 2 < width:
                    buffer.text(
                        self.rect.right() - 1 - len(shortcut) - 1, y,
                        shortcut,
                        line_style if i == bar.item_index
                        else shortcut_style,
                    )
            i += 1


class MenuBar(Widget):
    def __init__(self, menus: list) -> None:
        super().__init__()
        self.menus = menus
        self.active = False
        self.menu_index = 0
        self.item_index = 0
        self.checked = {}
        self.popup = None
        self.popup_border = Border.SINGLE
        self.size_hint = 1

    # -- state -------------------------------------------------------- #

    def current_items(self) -> list:
        return self.menus[self.menu_index][1]

    def set_checked(self, action: str, flag: bool) -> None:
        self.checked[action] = flag
        self.invalidate()

    def is_checked(self, action: str) -> bool:
        if action in self.checked:
            return self.checked[action]
        return False

    def title_x(self, index: int) -> int:
        """Column of a menu title — single source for bar and popup."""
        x: int = self.rect.x + 1
        i: int = 0
        while i < index:
            x += len(self.menus[i][0]) + 2
            i += 1
        return x

    # -- hotkeys ------------------------------------------------------ #

    def _make_opener(self, index: int):
        def handler() -> None:
            self.open(index)
        return handler

    def on_mount(self) -> None:
        if self.app is None:
            return
        self.app.add_hotkey("f10", self._make_opener(0))
        i: int = 0
        while i < len(self.menus):
            first: str = self.menus[i][0][:1].lower()
            if len(first) > 0:
                self.app.add_hotkey("alt+" + first, self._make_opener(i))
            i += 1

    # -- open / close ------------------------------------------------- #

    def _first_item(self) -> int:
        items: list = self.current_items()
        i: int = 0
        while i < len(items):
            if items[i][0] != "-":
                return i
            i += 1
        return 0

    def _show_popup(self) -> None:
        if self.popup is not None:
            self.app.remove_overlay(self.popup)
        items: list = self.current_items()
        width: int = 4
        i: int = 0
        while i < len(items):
            need: int = len(items[i][0]) + len(items[i][1]) + 6
            if need > width:
                width = need
            i += 1
        self.popup = _MenuPopup(self)
        self.popup.rect = Rect(self.title_x(self.menu_index),
                               self.rect.y + 1, width, len(items) + 2)
        self.app.add_overlay(self.popup)

    def open(self, index: int) -> None:
        if self.app is None:
            return
        self.menu_index = index
        self.active = True
        self.item_index = self._first_item()
        self._show_popup()
        self.app.set_key_grab(self)
        self.invalidate()

    def close(self) -> None:
        if self.popup is not None:
            self.app.remove_overlay(self.popup)
            self.popup = None
        self.active = False
        self.app.release_key_grab()
        self.invalidate()

    def activate_current(self) -> None:
        action: str = self.current_items()[self.item_index][2]
        self.close()
        if len(action) > 0:
            self.app.action(action)

    # -- input (routed via the key grab while active) ----------------- #

    def _move_menu(self, delta: int) -> None:
        self.menu_index = (self.menu_index + delta) % len(self.menus)
        self.item_index = self._first_item()
        self._show_popup()
        self.invalidate()

    def _move_item(self, delta: int) -> None:
        items: list = self.current_items()
        index: int = self.item_index
        while True:
            index = (index + delta) % len(items)
            if items[index][0] != "-":
                break
        self.item_index = index
        self.invalidate()

    def on_key(self, event) -> bool:
        if not self.active:
            return False
        if event.key == Key.LEFT:
            self._move_menu(-1)
        elif event.key == Key.RIGHT:
            self._move_menu(1)
        elif event.key == Key.UP:
            self._move_item(-1)
        elif event.key == Key.DOWN:
            self._move_item(1)
        elif event.key == Key.ENTER:
            self.activate_current()
        elif event.key == Key.ESCAPE or event.key == Key.F10:
            self.close()
        return True

    def on_mouse(self, event) -> bool:
        if event.kind != EventType.MOUSE_DOWN:
            return event.kind == EventType.MOUSE_UP
        i: int = 0
        while i < len(self.menus):
            start: int = self.title_x(i)
            if start <= event.x < start + len(self.menus[i][0]) + 2:
                if self.active and i == self.menu_index:
                    self.close()
                else:
                    self.open(i)
                return True
            i += 1
        return True

    # -- render ------------------------------------------------------- #

    def render_self(self, buffer: Buffer) -> None:
        style = self.theme_style("menu")
        chosen = self.theme_style("menu.selected")
        buffer.fill(self.rect, " ", style)
        i: int = 0
        while i < len(self.menus):
            title: str = " " + self.menus[i][0] + " "
            active_here: bool = self.active and i == self.menu_index
            buffer.text(self.title_x(i), self.rect.y, title,
                        chosen if active_here else style)
            i += 1
