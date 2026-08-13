"""A DCE-surviving class keeps its whole vtable.

Wrapped's dunders are invoked only implicitly — __str__ via print,
__lt__ via sorted, __eq__ via ==, __len__ via len() — so this fails if
dead-code elimination ever removes methods (it must only ever drop
entire unreferenced top-level definitions).
"""

from dce_hlp import Wrapped

items = [Wrapped(3), Wrapped(1), Wrapped(2)]
for item in sorted(items):
    print(item)
print(len(Wrapped(5)))
print(Wrapped(1) == Wrapped(1))
