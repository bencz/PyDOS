"""EDIT's palette: the classic blue-desktop theme with small tweaks."""

from pydos.tui.color import Color, Style
from pydos.tui.theme import Theme


def editor_theme():
    theme = Theme.turbo()
    theme.put("status", Style(Color.BLACK, Color.CYAN))
    theme.put("status.key", Style(Color.RED, Color.CYAN))
    return theme
