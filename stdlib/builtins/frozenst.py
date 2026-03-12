# frozenst.py - frozenset type stub for PyDOS (8.3 filename)
#
# Parsed by stdgen.cpp to generate stdlib.idx entries.

from _internal import internal_implementation, type_operators

@type_operators(contains="pydos_obj_contains_")
class frozenset:
    @internal_implementation("pydos_frozenset_union_")
    def union(self, other) -> frozenset: ...

    @internal_implementation("pydos_frozenset_intersection_")
    def intersection(self, other) -> frozenset: ...

    @internal_implementation("pydos_frozenset_difference_")
    def difference(self, other) -> frozenset: ...

    @internal_implementation("pydos_frozenset_symmetric_difference_")
    def symmetric_difference(self, other) -> frozenset: ...

    @internal_implementation("pydos_frozenset_issubset_")
    def issubset(self, other) -> bool: ...

    @internal_implementation("pydos_frozenset_issuperset_")
    def issuperset(self, other) -> bool: ...

    @internal_implementation("pydos_frozenset_isdisjoint_")
    def isdisjoint(self, other) -> bool: ...
