"""Semantic styles: widgets ask for names, themes answer with styles.

Lookup walks dotted names outward — "button.focus" falls back to
"button", then to "default" — so a theme only overrides what it wants:

    theme = Theme.turbo()
    theme.put("status", Style(Color.BLACK, Color.CYAN))
    style = theme.style("button.focus")
"""

from pydos.tui.color import Color, Style


class Theme:
    def __init__(self, styles: dict) -> None:
        self.styles = styles

    def style(self, name: str) -> Style:
        current: str = name
        while True:
            if current in self.styles:
                return self.styles[current]
            dot: int = current.rfind(".")
            if dot < 0:
                break
            current = current[:dot]
        if "default" in self.styles:
            return self.styles["default"]
        return Style(Color.LIGHT_GRAY, Color.BLACK)

    def put(self, name: str, style: Style) -> None:
        self.styles[name] = style

    @staticmethod
    def turbo() -> "Theme":
        """Blue-desktop palette in the spirit of the classic DOS IDEs."""
        return Theme({
            "default": Style(Color.LIGHT_GRAY, Color.BLUE),
            "desktop": Style(Color.LIGHT_GRAY, Color.BLUE),
            "frame": Style(Color.WHITE, Color.BLUE),
            "frame.title": Style(Color.YELLOW, Color.BLUE),
            "label": Style(Color.LIGHT_GRAY, Color.BLUE),
            "button": Style(Color.BLACK, Color.LIGHT_GRAY),
            "button.focus": Style(Color.WHITE, Color.GREEN),
            "check": Style(Color.LIGHT_GRAY, Color.BLUE),
            "check.focus": Style(Color.WHITE, Color.BLUE),
            "input": Style(Color.WHITE, Color.CYAN),
            "input.focus": Style(Color.WHITE, Color.CYAN),
            "input.placeholder": Style(Color.LIGHT_GRAY, Color.CYAN),
            "list": Style(Color.BLACK, Color.CYAN),
            "list.selected": Style(Color.WHITE, Color.GREEN),
            "table.header": Style(Color.WHITE, Color.CYAN),
            "progress": Style(Color.GREEN, Color.BLUE),
            "menu": Style(Color.BLACK, Color.LIGHT_GRAY),
            "menu.selected": Style(Color.WHITE, Color.GREEN),
            "menu.shortcut": Style(Color.RED, Color.LIGHT_GRAY),
            "status": Style(Color.BLACK, Color.LIGHT_GRAY),
            "status.key": Style(Color.RED, Color.LIGHT_GRAY),
            "dialog": Style(Color.BLACK, Color.LIGHT_GRAY),
            "dialog.title": Style(Color.BLUE, Color.LIGHT_GRAY),
            "shadow": Style(Color.DARK_GRAY, Color.BLACK),
            "textarea": Style(Color.YELLOW, Color.BLUE),
            "textarea.current": Style(Color.WHITE, Color.BLUE),
            "textarea.gutter": Style(Color.CYAN, Color.BLUE),
            "textarea.match": Style(Color.BLACK, Color.YELLOW),
            "scrollbar": Style(Color.CYAN, Color.BLUE),
        })

    @staticmethod
    def mono() -> "Theme":
        """Monochrome palette for MDA/Hercules and golden tests."""
        return Theme({
            "default": Style(Color.LIGHT_GRAY, Color.BLACK),
            "desktop": Style(Color.LIGHT_GRAY, Color.BLACK),
            "frame": Style(Color.WHITE, Color.BLACK),
            "frame.title": Style(Color.WHITE, Color.BLACK),
            "button": Style(Color.BLACK, Color.LIGHT_GRAY),
            "button.focus": Style(Color.WHITE, Color.BLACK),
            "input": Style(Color.BLACK, Color.LIGHT_GRAY),
            "list.selected": Style(Color.BLACK, Color.LIGHT_GRAY),
            "menu": Style(Color.BLACK, Color.LIGHT_GRAY),
            "menu.selected": Style(Color.WHITE, Color.BLACK),
            "status": Style(Color.BLACK, Color.LIGHT_GRAY),
            "dialog": Style(Color.BLACK, Color.LIGHT_GRAY),
            "shadow": Style(Color.DARK_GRAY, Color.BLACK),
        })
