"""Golden test for pydos.tui.theme: hierarchical fallback and presets."""

from pydos.tui.color import Color, Style
from pydos.tui.theme import Theme

theme = Theme.turbo()
print(theme.style("button").attr())
print(theme.style("button.focus").attr())
print(theme.style("button.focus.deep").attr())
print(theme.style("unknown.name").attr())
print(theme.style("input.placeholder").attr())

theme.put("status", Style(Color.BLACK, Color.CYAN))
print(theme.style("status").attr())
print(theme.style("status.key").attr())

empty = Theme({})
print(empty.style("anything").attr())

mono = Theme.mono()
print(mono.style("desktop").attr())
print(mono.style("textarea.current").attr())
