# PyDOS compiler architecture

This document is the engineering baseline for compiler work after the audit of
2026-08-06. It describes the implementation that exists in the repository,
the contracts on which it depends, and the changes required to approach
Python 3.12 semantics without sacrificing the 8086 and 386 targets.

It is intentionally not a feature checklist. The compatibility roadmap records
which Python features are available. This document records where a feature is
implemented, which representation owns its semantics, and which invariants a
change must preserve.

## Design goals

PyDOS compiles a Python source program ahead of time to Open Watcom assembly and
links it with a small DOS runtime.

The architectural goals are:

1. Preserve Python 3.12 observable behavior wherever DOS can support it.
2. Keep high-level behavior in Python-backed stdlib modules when possible.
3. Keep C runtime code for primitive representation, DOS services, memory,
   reference ownership, dispatch primitives, and operations whose performance
   or ABI requires native code.
4. Produce genuine 8086 code in real mode and 386 code in protected mode.
5. Make every unsupported construct a compile-time diagnostic. Code generation
   must never emit a comment for an unknown operation and report success.
6. Optimize only after proving that an optimization preserves dynamic Python
   behavior, exceptions, evaluation order, object identity, and ownership.

## Repository map

The compiler is in `compiler/`.

| Area | Primary files | Responsibility |
| --- | --- | --- |
| Tokens | `token.h`, `lexer.h`, `lexer.cpp` | Indentation-aware tokenization, literals, f-string token state |
| AST | `ast.h`, `ast.cpp` | Tagged AST nodes and compilation-lifetime arena allocation |
| Parser | `parser.h`, `parser.cpp` | Recursive-descent Python grammar, type syntax, private-name transformation |
| Static semantics | `types.*`, `sema.*` | Scopes, symbols, closure discovery, conservative type hints |
| Generic specialization | `mono.*` | Current AST cloning and type substitution for generic classes/functions |
| PIR construction | `pir.*`, `pirbld.*` | CFG and SSA-like Python intermediate representation |
| PIR analyses | `pirdom.*`, `pirtyp.*`, `piresc.*` | Dominance, loops, type facts, escape facts |
| PIR optimization | `piropt.*`, `pirspc.*`, `pirutil.*` | SCCP, mem2reg, GVN, LICM, devirtualization, specialization, scope insertion |
| PIR serialization | `pirsrlz.*`, `stdscan.*`, `stdbld.*`, `pirmrg.*` | Python-backed stdlib compilation, index loading, reachability merge |
| Flat IR | `ir.*`, `pirlwr.*`, `iropt.*` | Backend-oriented three-address representation and late optimization |
| Code generation | `codegen.*`, `cg8086.*`, `cg386.*`, `regalloc.*` | Shared discovery/dispatch and target-specific Open Watcom assembly |
| Imports | `modpath.*`, `modscan.*`, source linker in `main.cpp` | Source resolution, symbol scan, current source flattening |

The native runtime is in `runtime/`. Python-backed builtins and library modules
are in `stdlib/`. DOS integration tests are selected by `runtests.bat` and can
be built and run from Linux with `tests/run_dos_linux.sh`.

## Compilation pipeline

The command-line driver in `compiler/main.cpp` owns the pass order.

```text
source files
    |
    v
Lexer -> Parser -> linked AST -> SemanticAnalyzer -> Monomorphizer
                                                    |
                                                    v
                                              PIRBuilder
                                                    |
                               reachable stdlib PIR merge
                                                    |
                                                    v
        DBE -> SCCP -> DBE -> mem2reg -> type analysis -> escape analysis
              -> devirtualization -> specialization -> DIE -> GVN -> LICM
              -> DIE -> arena-scope insertion
                                                    |
                                                    v
                                               PIRLowerer
                                                    |
                                                    v
              flat constant fold -> strength reduce -> copy propagation
                    -> dead-code elimination -> peephole
                                                    |
                                                    v
                                  8086 or 386 code generator
                                                    |
                                                    v
                                  WASM -> WLINK -> DOS executable
```

PIR is released after lowering. This is important when the compiler itself is
built for a memory-constrained environment and also prevents the backends from
depending on both representations at once.

## Storage and lifetime

### Lexer strings

Tokens may point into storage owned by a `Lexer`. AST string fields therefore
do not uniformly own their bytes. The source linker keeps imported-module lexer
objects alive until compilation ends. Deleting one earlier can leave dangling
AST strings.

This borrowing contract is fragile. The target architecture is an interned
compiler string pool whose lifetime is the compilation. AST, symbol, PIR, and
IR names should all refer to that pool. This removes intentionally leaked lexer
objects and the many local `str_dup` variants.

### AST arena

`ast_alloc`, `param_alloc`, `cmpop_alloc`, and `name_array_alloc` use one global
arena that is released by `ast_free_all`. Individual AST deletion is a no-op.
All AST transformations must allocate replacement AST data in the same arena or
in an explicitly owned compilation arena.

The parser and monomorphizer currently allocate some names and arrays directly
with `malloc`. Those allocations survive until process exit and do not follow a
single ownership rule. This is compiler memory leakage, not a DOS program leak,
but it must be removed as the grammar and generic metadata grow.

### Sema and type objects

Scopes and symbols are heap allocated. Popping a scope frees `Symbol` records,
but several strings, `ClassInfo` records, member copies, and closure-name arrays
outlive the scope. Type objects are managed by the type subsystem and are used
as non-owning hints by PIR and flat IR.

An optimization must treat a missing, `Any`, or conflicting type hint as
unknown. A hint is not a runtime guard.

### PIR and flat IR

PIR owns its blocks and instructions. Values are integer IDs local to a
function. Flat IR reuses PIR value IDs as virtual temporaries, then adds local
slots for allocas and phi nodes. Strings copied into the IR constant pool are
owned by the IR module.

## Parsing and Python names

The parser is recursive descent with explicit precedence levels. It builds
linked lists through `ASTNode::next` and uses a separate linked `Param` type for
function signatures.

Private-name mangling belongs to lexical analysis of identifiers in a class
body, not to runtime attribute lookup. The rule is:

```text
__name      inside class _Class  -> _Class__name
__name__                         -> unchanged
name_                            -> unchanged
```

Leading underscores are removed from the class name before constructing the
prefix. The transformation must be applied consistently to definitions and
uses that are lexically inside the class, including method names, simple names,
attribute components, slots, annotations where names are evaluated, and
pattern-bound names. Keyword argument names are not identifiers in the caller's
class scope and must not be mangled.

The current parser keeps a fixed class stack of 16 entries. Depth must be
checked before indexing; silently incrementing beyond the array is memory
corruption. A dynamic stack is preferable.

PEP 701 f-strings require the f-string expression to use the normal Python
tokenizer and parser, including nested strings, matching quote characters,
comments, newlines, and nested replacement fields. A separate restricted
expression scanner cannot implement Python 3.12 correctly.

## Scopes, symbols, and closures

Python decides local names for an entire code block before executing it. A name
assigned anywhere in a function is local unless declared `global` or
`nonlocal`. This requires a declaration-collection pass before resolving reads.

The current semantic analyzer mostly discovers declarations while walking in
source order. Recent fixes correctly prevent a local assignment from mutating
an unrelated outer symbol and propagate transitive closure captures, but a true
symbol-table prepass remains necessary for:

- reads before a later local assignment and `UnboundLocalError`;
- branch-defined and repeatedly defined nested functions;
- comprehensions and walrus targets;
- class bodies, whose lookup rules differ from function closures;
- `global` and `nonlocal` validation independent of statement order;
- annotations and type-parameter scopes.

Each function currently stores at most 32 cell variables and 32 free variables.
PIR materializes each captured binding as a `PYDT_CELL`, stores cells in a
closure list, and attaches the list to the function object.

Generated direct calls use `PIR_SET_CLOSURE`; dynamic runtime calls set the
global `pydos_active_closure` from the function object. The callee copies its
closure cells into local temporaries on entry. DOS execution is single threaded,
so a global handoff is viable, but it is stateful ABI and must be restored or
fully consumed before reentrant callbacks. Generators need their closure stored
with the suspended generator state; a wrapper and resume function cannot rely
on whatever closure happens to be globally active later.

## Calls and signatures

Python call order is:

1. evaluate the callable;
2. evaluate every argument left to right;
3. bind positional, starred, keyword, and double-starred arguments;
4. invoke the resulting callable.

PIR uses `PIR_PUSH_ARG` followed by a call/build instruction. The backend does
not push immediately; it accumulates temporary IDs and consumes them at the
next call or collection builder. Consequently, builder code must evaluate all
nested expressions before emitting the contiguous push bundle.

The present fixed function ABI records an argument count and a tuple of trailing
defaults. It is not sufficient to model positional-only parameters, keyword-only
parameters, arbitrary `*args`, arbitrary `**kwargs`, duplicate keywords, or
precise CPython errors. The durable design is compact signature metadata plus
one binder shared by direct and indirect calls. A direct call is legal only
after binding is complete and the callable identity is proven.

Name-based direct-call selection is unsafe after rebinding, decoration, branch
assignment, or aliasing. Nested qualified assembly labels solve symbol
collisions, but do not prove that a Python name still denotes that label.

## Classes, MRO, descriptors, and vtables

The runtime class dictionary, C3 MRO, and descriptor protocol are the source of
truth. A vtable is a cache for a proven method lookup, not a parallel object
model.

The required lookup order for an instance attribute is:

1. data descriptor found through the class MRO;
2. instance dictionary or slot storage;
3. non-data descriptor or ordinary class attribute through the MRO;
4. `__getattr__` fallback.

Class lookup must handle metaclass data descriptors before the class namespace.
Assignment and deletion must honor descriptor setters/deleters and slots.

Generated vtables currently have fixed limits of 32 classes and 64 methods per
class, with at most seven secondary bases. These limits must be diagnosed, not
silently truncated. Guarded direct method calls must check the runtime class or
version that justified the cache. Any mutation that can change lookup must
invalidate the guard.

`__set_name__` receives the owning class object and the original class namespace
key as a string. Name mangling happens before this call. Descriptor methods
must receive properly bound arguments through the same general callable
protocol as other methods.

## Python 3.12 type parameters and aliases

PEP 695 introduces runtime objects and lexical scopes:

- classes and functions expose `__type_params__`;
- type aliases are runtime type-alias objects;
- bounds and constraints can be evaluated lazily;
- `TypeVarTuple` and `ParamSpec` preserve their kind;
- a parameterized generic normally does not create a new observable runtime
  class with duplicated class state.

The current monomorphizer clones generic AST definitions, substitutes types,
renames the clone, and removes the original definition. This can reduce code
cost for primitive specializations, but it changes identity, class attributes,
reflection, and decorator execution. It also risks unbounded code growth.

The architecture target is:

1. one observable runtime class/function with Python 3.12 metadata;
2. erased representation for ordinary object behavior;
3. optional internal specialized implementations selected behind guards;
4. a cache keyed by canonical type arguments;
5. no duplicated observable class dictionary or class counters;
6. limits and unsupported pack forms reported explicitly.

Until that model exists, AST monomorphization must be considered an internal
optimization with known compatibility limits, not a complete implementation of
PEP 695.

The current generic alias registry is a fixed, analyzer-global array of 64
entries. It must become scope-aware. Type parameters must live in the alias's
annotation scope and must not leak into the surrounding module scope.

## PIR contract

PIR is the semantic optimization representation. It contains explicit basic
blocks, predecessor/successor edges, SSA values, phi nodes, exception edges,
generator state operations, cells, object operations, and ownership scopes.

Every PIR opcode needs one authoritative descriptor containing:

- operand count and operand kinds;
- result kind and ownership;
- whether it may raise;
- whether it reads or writes mutable state;
- whether it allocates;
- whether it is a terminator;
- whether it can be moved, eliminated, serialized, or lowered;
- the flat IR lowering rule.

Today these properties are duplicated across `pirutil.cpp`, `pirtyp.cpp`,
`piresc.cpp`, `piropt.cpp`, `pirspc.cpp`, `pirlwr.cpp`, serialization, and
codegen prescans. Adding an opcode without updating every switch can silently
miscompile code. Central opcode metadata and a PIR verifier are required.

The verifier must run after PIR construction, after stdlib merge, after each
mutating optimization in debug builds, and before lowering. It must check:

- value definitions and operand ranges;
- one definition per SSA value;
- phi predecessor agreement;
- block terminators and CFG edge symmetry;
- exception targets;
- instruction result types;
- push-bundle balance;
- generator state indices;
- all fixed-capacity limits.

## Type inference and specialization

The type lattice must be conservative. At a control-flow join:

```text
int joined with int      -> int
int joined with unknown  -> unknown
int joined with str      -> unknown
```

Unknown is not a bottom value that can be discarded. It means the runtime value
may be anything. The current merge helper can preserve a known type when another
incoming path is unknown, which is unsound.

Specialization may replace boxed dynamic arithmetic only when both operands are
proven exact primitive values and the replacement preserves overflow,
exceptions, and result type. An annotation or a previous assignment is not
proof after a dynamic call or merge. Booleans also require care because
`bool` is a subclass of `int` but arithmetic results are normally `int`.

Unboxing must be guarded unless the producer itself proves the object tag.

## Optimization safety

Most Python operations are observably effectful. They can invoke user methods,
raise exceptions, allocate objects, or inspect mutable state.

These operations are not generally pure:

- arithmetic and rich comparison on boxed objects;
- containment and iteration;
- attribute and subscript access;
- truth testing;
- formatting and conversion;
- calls, including descriptor calls;
- allocation, boxing, and collection construction.

The flat IR optimizer currently treats several boxed arithmetic and comparison
operations as pure. Dead-code elimination can therefore remove an unused
`obj.__add__()` or `obj.__eq__()` call and its exception. This must be fixed.

Flat constant propagation also walks a linear instruction list without a CFG.
Facts must be cleared at labels and control-flow boundaries, or the pass must be
replaced with PIR SCCP. The preferred direction is to keep semantic
optimizations in PIR and restrict flat IR optimization to target-safe local
peepholes.

LICM requires both purity and guaranteed execution. Moving an allocation or a
potentially raising operation out of a loop changes behavior. GVN requires
alias-aware read classification and dominance. The conservative answer is to
leave a Python operation in place when proof is incomplete.

Escape analysis controls arena-scope reference tickets, so an error is a memory
safety issue, not only a missed optimization. It must model every persistent
store, cell, closure, generator local, class/instance attribute, container edge,
return, yield, exception, and unknown call.

## Lowering to flat IR

`PIRLowerer` assigns one label to each PIR block, maps allocas to local slots,
maps SSA IDs to temporaries, and lowers phi nodes through predecessor stores and
merge-block loads.

Flat IR overloads integer fields according to opcode. For example, `dest` can
mean a temporary, a local slot, a constant index, or the value of a subscript
store. This makes generic analysis error-prone. Any flat IR pass must use
opcode-specific metadata rather than assuming every field is a temporary.

Imports currently lower to `IR_NOP`. This is valid only because supported
source imports have already been linked and their names materialized. Any import
that still has runtime meaning must be rejected until a real module operation
exists.

The lowerer must report every unsupported PIR opcode. The code generator must
likewise fail compilation on every unsupported flat opcode. An emitted
`UNIMPLEMENTED` comment is not a valid result.

## Backend ABI

### 8086

The 8086 backend emits `.8086` and `.MODEL LARGE`.

- Code calls and generated functions are far.
- A Python object pointer occupies four bytes as segment:offset.
- Function results use `DX:AX`.
- Parameters start at `BP+6` after the far return address and saved BP.
- Locals and temporaries occupy four-byte stack slots.
- Runtime calls use cdecl and caller cleanup.
- DS is restored to DGROUP after far calls.
- Generated functions are split into separate code segments to avoid the
  64 KiB code-segment limit.
- The backend must not emit 80186+ instructions such as `push imm`.
- 32-bit integer helpers use the Open Watcom 8086 runtime ABI.

The large memory model is appropriate for the current object layout because
heap objects and data can cross segments. Near pointers may still be used for
verified objects inside one stable segment, but mixing near and far pointer
contracts is more dangerous than the bytes it saves.

### 386

The 386 backend emits `.386p` and `.MODEL FLAT` and links with CauseWay.

- Python object pointers are 32-bit linear addresses.
- Results use `EAX`.
- Locals and temporaries occupy four-byte stack slots.
- Generated calls and runtime calls are near cdecl calls.
- The executable links with `clib3s` and the 32-bit runtime compiled with
  `PYDOS_32BIT`.

Both backends must implement exactly the same flat IR semantics. Backend parity
tests are mandatory for every new opcode.

## Runtime ownership and exceptions

Runtime functions use explicit reference ownership. A new object or lookup
documented as owned must eventually be decremented or transferred. Parameters
are borrowed. Function returns are owned.

The runtime combines reference counting, cycle collection, and compiler-inserted
arena-scope tickets. Ownership annotations on PIR operations must agree with the
C implementation.

Exceptions are represented by a pending exception object. Generated code checks
the pending state after operations that may raise and branches to explicit
landing blocks. There is no `setjmp`/`longjmp` stack transfer. Any opcode added
to the may-raise set must receive a propagation check from PIRBuilder and must
not be moved across handlers.

Generator suspension serializes Python locals into the generator object. It
must also preserve pending delegation state, closure state, cleanup state, and
the return value carried by `StopIteration` for full `yield from` behavior.

## Stdlib boundary

`stdlib.idx` contains:

- builtin function metadata;
- primitive-type method metadata;
- exception mappings;
- serialized PIR for Python-backed builtins.

The stdlib builder compiles Python-backed implementations to PIR. The user
compiler merges only reachable functions and their transitive PIR dependencies.
Codegen registers linked Python-backed primitive methods in runtime vtables.

This boundary is the intended organization:

- primitive storage and primitive operations that require speed remain in C;
- descriptors, ABC behavior, dataclasses, file wrappers, TUI abstractions, and
  other high-level policies remain in Python when the compiler supports them;
- Python modules may call narrow `_internal` primitives, but user-visible APIs
  should not expose DOS ABI details.

The current source linker prepends imported module bodies into one global AST.
This is textual flattening, not Python module semantics. It loses module
namespaces, initialization identity, cyclic-import state, and clean collision
handling. Source requirement discovery is also token-based, so an unrelated
identifier named `open` can pull in file support.

The target is a module graph with one namespace and one initialization state per
module. Reachability can still be resolved ahead of time for DOS.

## Fixed limits that require diagnostics

The implementation contains fixed arrays suitable for DOS, but exceeding one
must be an explicit compiler error. Important current limits include:

- parser class nesting: 16;
- sema expression type entries: 4096;
- generic aliases: 64;
- closure cell/free variables: 32 each;
- call arguments: 64 in compiler stages;
- dynamic runtime function call arguments: currently 8;
- PIR generator resume states: 32;
- tracked generator local names: 64;
- flat IR locals: 256;
- PIR dominance blocks: 1024;
- natural loops: 64;
- classes per module vtable table: 32;
- methods per class vtable: 64;
- additional bases: 7;
- stdlib metadata and serialized PIR limits from `stdscan.h`.

Silent truncation is a miscompile. Every insertion site must check its capacity.

## Audited baseline for the new tests

`tests/manglepy.py` is the executable regression target. On the audited tree it:

- parses and compiles for both targets;
- assembles and links for both targets;
- produces the same runtime failure on both targets in section 5;
- raises `TypeError: attribute name must be string` during descriptor-backed
  `ContextCounter` construction;
- already diverges earlier in generator delegation, empty tuple pattern output,
  private-name dictionary materialization, and missing `AttributeError` for a
  direct private lookup.

`tests/manglep2.py` is a broader Python 3.12 conformance destination. It has no
expected-output file yet and currently stops in parsing. Its feature groups
include:

1. PEP 695 runtime metadata, bounds, constraints, packs, and aliases;
2. private generic parameters and complete name mangling;
3. slots and descriptors;
4. deep closures and nonlocal state;
5. generator send, return, and delegation;
6. PEP 701 f-strings;
7. PEP 709 comprehension semantics and reflection;
8. structural pattern matching and context managers;
9. data-model hooks and copying;
10. multiple inheritance and C3 MRO;
11. `override`, `TypedDict`, and `Unpack` metadata;
12. the Python-level buffer protocol;
13. `sys.monitoring`.

It must not be made green with empty modules, fake metadata, constant answers,
or functions created only for the test. Each group should be enabled after its
general implementation has focused regressions on both DOS targets.

## Required correction order

The order below minimizes the risk of debugging a frontend feature through an
unsound optimizer or backend.

1. Add PIR and flat IR verification and make unsupported lowering/codegen fatal.
2. Fix type-lattice joins and remove unsafe specialization proofs.
3. Make flat IR optimization conservative for boxed Python operations and CFG
   boundaries.
4. Complete lexical symbol collection, scope resolution, and closure/generator
   capture.
5. Fix call binding and callable evaluation using uniform signature metadata.
6. Complete class dictionary, slots, descriptor, and name-mangling semantics.
7. Implement generator delegation as persistent state, including completion
   values and exception forwarding.
8. Implement PEP 701 using the ordinary tokenizer/parser inside replacement
   fields.
9. Replace observable AST monomorphization with erased generic identity plus
   guarded internal specialization and Python 3.12 metadata.
10. Replace source flattening with a module graph.
11. Add the remaining Python 3.12 data-model and stdlib layers in focused,
    independently testable increments.

## Validation workflow

For any semantic change:

1. Run the source with CPython 3.12 and capture exact expected output.
2. Add a focused test that isolates one language invariant.
3. Build the native compiler and `stdlib.idx`.
4. Inspect `--dump-pir`, `--dump-types`, and `--dump-escape` when the change
   affects optimizer facts or ownership.
5. Run the focused test in both targets:

```sh
tests/run_dos_linux.sh all test_name
```

6. Run the complete DOS suite in both targets.
7. Run native runtime tests and `git diff --check`.
8. When an optimization is involved, compare normal output with the relevant
   `--no-*` flags. A behavior change when disabling an optimization indicates a
   compiler bug, not a test problem.

No test is complete merely because assembly was emitted. Completion requires
parse, semantic analysis, PIR verification, lowering, assembly, link, execution,
output comparison, and parity between 8086 and 386.
