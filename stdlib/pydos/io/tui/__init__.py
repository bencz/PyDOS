"""Pythonic text user-interface toolkit for DOS."""

from pydos.io.tui.canvas import Canvas
from pydos.io.tui.clock import delay, ticks_ms
from pydos.io.tui.constants import Color, Key
from pydos.io.tui.keyboard import key_available, read_key, wait_key
from pydos.io.tui.screen import Screen
from pydos.io.tui.widgets import Application, Button, Dialog, Label, TextInput
from pydos.io.tui.widgets import Widget
from pydos.io.tui.widgets import create_button, create_label
