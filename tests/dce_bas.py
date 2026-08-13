"""Dead-code elimination must not change behavior.

Exercises the DCE roots: direct call, import alias, base class kept via
a live subclass, transitive helper reached only from a method body, and
a decorated definition whose decorator side effect still runs.  The
golden output is identical to CPython, which performs no DCE at all.
"""

from dce_hlp import Circle, used_function
from dce_hlp import aliased_helper as helper_alias

print(used_function(21))
print(helper_alias())
shape = Circle(4)
print(shape.area_times_ten())
print(shape.describe())
