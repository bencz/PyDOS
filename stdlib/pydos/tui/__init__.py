"""Pythonic text user-interface toolkit for DOS.

Lean facade: only the immediate core is re-exported, because everything a
facade imports is linked into the executable.  Widgets, themes, input and
the application framework are imported from their own modules:

    from pydos.tui import Buffer, Color, Key, Rect, Screen, Style
    from pydos.tui.app import App
    from pydos.tui.widgets.button import Button
"""

from pydos.tui.geometry import Rect
from pydos.tui.color import Color, Style
from pydos.tui.buffer import Buffer
from pydos.tui.keys import Key
from pydos.tui.screen import Screen, Cursor
