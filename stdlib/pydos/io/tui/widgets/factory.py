"""Convenience constructors for declarative application code."""

from pydos.io.tui.widgets.button import Button
from pydos.io.tui.widgets.label import Label


def create_button(x, y, width, text, on_click, hotkey=None):
    return Button(x, y, width, text, on_click, hotkey)


def create_label(x, y, text, width=0):
    return Label(x, y, text, width)
