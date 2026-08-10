# PyDOS execution architecture

This document defines the planned execution architecture for PyDOS. It extends
the existing ahead-of-time compiler without replacing the 8086 and 386
backends. The same frontend, Python semantics, optimized PIR and stdlib sources
must serve every execution engine.

## Goals

1. Keep the current native compiler operational throughout the migration.
2. Let 8086 applications execute programs larger than conventional memory.
3. Keep high-level stdlib implementations in Python.
4. Support native, VM and mixed execution without duplicating language
   semantics.
5. Make bytecode portable across DOS, x64, ARM, PowerPC and future targets.
6. Preserve reflection, descriptors, closures, generators, exceptions and
   dynamic dispatch in every mode.
7. Reject unavailable modes and unsupported operations explicitly.

## Non-goals for the first VM

- runtime parsing of Python source;
- runtime semantic analysis or PIR optimization;
- a JIT for 8086;
- replacing primitive object storage with Python implementations;
- using `stdlib.idx` directly as an executable library;
- silently falling back to native execution when VM execution was requested.

## Shared compilation pipeline

```text
Python source
    |
    v
Lexer -> Parser -> AST -> Sema -> optimized PIR
                                      |
                      +---------------+---------------+
                      |                               |
                      v                               v
               native lowering                bytecode lowering
                      |                               |
                      v                               v
              8086/386/future ASM                 PBC module
```

PIR remains the semantic optimization representation. PBC is a compact,
verified execution representation. It must not expose compiler pointers, AST
nodes, SSA implementation details or host structure layouts.

## Execution modes

### `native`

Every reachable Python function is lowered to target assembly and linked into
the executable. This is the current behavior and remains the compatibility
baseline.

### `vm`

Python functions are lowered to PBC and executed by a portable runtime VM.
Application and stdlib code may remain in external files and be loaded by
module or page.

### `hybrid`

One program contains native and PBC functions. Native code is used for proven
hot paths and target primitives. PBC is used for cold functions, large modules
and high-level stdlib code. Both paths use one callable protocol and one Python
object model.

### `auto`

Target policy selects an available implementation. During the first phase only
the native engine exists, therefore `auto` resolves to `native`. Once the VM is
validated, the intended defaults are:

| Target | Intended automatic policy |
| --- | --- |
| DOS 8086 | hybrid |
| DOS 386 | native or hybrid based on program budget |
| portable package | VM |
| modern native targets | native, with optional VM and JIT tiers |

The compiler must always report the effective mode in verbose output.

## Artifact roles

`stdlib.idx` remains a compiler catalog. It contains signatures, runtime type
metadata, builtin mappings and serialized PIR used by reachability merging.

Executable artifacts are separate:

```text
APP.EXE   native bootstrap, runtime, VM and optional native functions
APP.PBC   application bytecode and metadata
PYDOS.PYL executable Python stdlib archive
```

DOS distributions may use 8.3 file names. The logical format and versioning do
not depend on those names.

## Stable code references

The current runtime primarily identifies generated functions with direct code
pointers. Direct pointers cannot describe unloaded bytecode and cannot remain
valid when an overlay cache evicts code.

The target model is a stable logical reference:

```text
kind
module_id
function_id
native entry, when resident
```

Kinds initially include builtin primitive, native function and PBC function.
The public callable path dispatches through the logical reference. A guarded
native call remains possible after the compiler proves the code kind and
identity.

The first concrete runtime descriptor is `PyDosCodeRef`. It is reference
counted independently of Python objects and has three executable kinds:
native fixed-ABI function, builtin `argc/argv` function and PBC function. A
PBC reference keeps a stable module descriptor pointer plus a 16-bit function
index. Bound methods share the descriptor instead of copying a raw code
pointer. Module metadata must remain resident while one of its code references
exists; executable bytecode pages may still be evicted independently.

All first-class function calls now pass through one gateway after Python
argument binding. The gateway preserves and restores the active closure across
nested calls. Native functions retain their eight-slot generated ABI, while
PBC functions may bind up to 255 positional slots without imposing that ABI
limit on the VM.

Functions, bound methods, general vtable method entries, PBC generators,
PBC coroutines and reflected code objects now share stable code references.
The indexed dunder slots deliberately remain native pointers as the compact
8086 fast path; a PBC method remains available through the authoritative
general method table. Traceback frames and pageable module ownership remain
part of the loader work.

## PBC format requirements

All compiler, runtime, shared-format and shipped stdlib source filenames obey
DOS 8.3. Python module names remain unchanged: the source resolver first tries
the logical path and then a deterministic alias obtained by truncating every
path component to eight characters. For example, `dataclasses` is stored in
`dataclas.py`, while `pydos.io.tui.widgets.application` is stored in
`pydos/io/tui/widgets/applicat.py`. PBC implementation files use the same rule,
including `pbcarc.cpp`, `pdospbc.h` and `pdospyl.h`.

The format starts versioned at the file header and uses fixed-width integer
encodings. It defines byte order explicitly and never writes a C structure with
`fwrite(sizeof(struct))`.

Required sections are:

1. header and target-independent feature flags;
2. string table;
3. constant table;
4. symbol and import table;
5. function descriptors;
6. bytecode bodies;
7. exception regions;
8. class and method metadata;
9. signature and closure metadata;
10. source and traceback metadata;
11. optional integrity checksum.

The first implemented container revision has this 24-byte header:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 4 | `PYBC` magic |
| 4 | 2 | major version |
| 6 | 2 | minor version |
| 8 | 2 | header size |
| 10 | 2 | section-entry size |
| 12 | 4 | header feature flags |
| 16 | 2 | section count |
| 18 | 2 | reserved, must be zero |
| 20 | 4 | complete file size |

Each 16-byte section-directory entry contains type, flags, file offset, byte
size and logical item count. Payloads are aligned to four bytes. The structural
verifier already rejects incompatible versions, unknown required sections,
unknown flags, duplicate types, truncated directories, misalignment,
out-of-range data and overlapping sections. Unknown sections are accepted only
when marked optional.

Every section has an offset and size. Readers validate integer overflow,
section overlap, index ranges, instruction boundaries, branch targets, stack or
register bounds, exception regions and declared limits before execution.

The current function record is 28 bytes:

| Offset | Width | Field |
| --- | --- | --- |
| 0 | 2 | name symbol |
| 2 | 2 | function flags |
| 4 | 4 | code-section offset |
| 8 | 4 | bytecode size |
| 12 | 2 | argument count |
| 14 | 2 | local count |
| 16 | 2 | maximum operand stack |
| 18 | 2 | first exception record |
| 20 | 2 | exception record count |
| 22 | 2 | signature index, currently `0xffff` |
| 24 | 2 | closure cell count |
| 26 | 2 | reserved, must be zero |

Binary64 constants are canonical IEEE-754 little-endian bytes. The writer
detects both little-endian and big-endian binary64 hosts and rejects unknown or
mixed representations instead of copying native bytes ambiguously.

## Bytecode design rules

- Prefer compact one-byte opcodes with explicit 8-bit and 16-bit operand forms.
- Use logical indices rather than pointers.
- Keep Python evaluation order and exception edges explicit.
- Keep ownership behavior derivable from authoritative opcode metadata.
- Allow decoding without unaligned host loads.
- Bound frame size, arguments, constants, branches and nesting for 8086.
- Permit a verifier to reject a module without allocating its full contents.
- Preserve a bytecode offset as the suspension point for generators and
  coroutines.

The initial VM should be a simple switch interpreter. Direct-threaded dispatch
and JIT compilation are later optimizations and must not influence bytecode
semantics.

### Initial instruction families

The first instruction metadata table is implemented and shared with the code
verifier. It currently defines these families:

| Family | Operations |
| --- | --- |
| constants | `LOAD_NONE`, `LOAD_TRUE`, `LOAD_FALSE`, 8/16-bit constant indices |
| frame | 8/16-bit local loads and stores, 16-bit globals |
| stack | `POP_TOP`, `DUP_TOP` |
| arithmetic | Python add, subtract, multiply, division, modulo and power |
| unary | positive, negative, truth negation and bitwise negation |
| comparison | equality and ordered rich comparisons |
| control flow | relative 16-bit jump and conditional jumps |
| calls | dynamic callable plus an 8-bit argument count |
| construction | list, tuple and set with an 8-bit item count |
| objects | item and attribute reads and writes |
| iteration | iterator creation and a two-edge `FOR_ITER16` |
| functions | return, raise, yield and function-reference creation |
| exceptions | pending-exception branch and clear |
| closures | make local cell, load/store cell, load/store free variable |

Opcode metadata records operand kind and width, fixed stack input/output and
semantic flags such as `may_raise`, branch, terminator, suspension and variable
stack effect. The verifier uses that table rather than duplicating instruction
sizes or effects.

Each function body is limited to 65,535 bytes in the first format. This keeps
relative branch offsets and 8086 function paging bounded. A module may contain
many functions and therefore exceed one segment as a whole.

The code verifier performs two passes. The decoding pass identifies every
instruction boundary and validates operand indices. The control-flow pass
propagates stack depth from the function entry and validates every reachable
edge. It rejects:

- unknown or truncated instructions;
- constants, locals, globals, symbols and functions outside their tables;
- calls and collection builders above declared limits;
- branches outside the function or into operand bytes;
- stack underflow and declared-stack overflow;
- different stack depths arriving at one instruction;
- reachable paths that fall past the function body;
- invalid success and exhaustion depths for iterator loops.

## Module loader and cache

The loader owns module identity and initialization state. It replaces the
current source-body flattening for VM modules.

```text
unloaded -> loading -> initialized
               |
               v
             failed
```

The cache separates metadata from executable bodies. Stable module and function
descriptors may remain resident while bytecode pages are evicted. Active frames
pin their page. An LRU policy may evict only unpinned pages.

On 8086 the cache has an explicit conventional-memory budget. EMS or XMS may
be used later as backing storage, but Python objects used by each opcode remain
in directly addressable memory.

## Runtime boundaries

The C runtime continues to own primitive representation, memory management,
GC, exception state, fundamental dispatch and platform services. High-level
algorithms stay in Python and can be emitted as native code or PBC.

The VM depends on narrow runtime primitives. It must not introduce second
implementations of descriptors, MRO, call binding or exception semantics.
Native and VM paths share those services.

The host VM currently executes over the real `PyDosObj` representation. It
owns references held by its locals, operand stack and captured closure, and
releases them on every normal or exceptional exit. Closures are tuples of
tracked `PYDT_CELL` objects, so a nested PBC function can outlive its creator
without copying captured values.

Suspended PBC frames store locals, the operand stack and the captured closure
in GC-visible runtime containers. A generator records the bytecode offset
immediately after `YIELD_VALUE`; resumption pushes the value supplied by
`send()` as the result of the yield expression. `throw()` injects its exception
at the suspended yield offset and uses the same exception-region dispatcher as
ordinary opcode failures. `close()` therefore executes matching cleanup paths,
suppresses an escaping `GeneratorExit`, and reports a generator that yields
during close. Coroutines use the same persistent frame representation.

The module verifier rejects a suspending opcode in a function that is not
marked as a generator or coroutine. Completed, closed and failed suspended
frames release their locals, stack, closure and code reference immediately;
cycles reachable only through a released frame remain recoverable by the cycle
collector.

Platform services are separated from semantics:

```text
runtime/core
runtime/vm
runtime/platform/dos
runtime/platform/posix
runtime/platform/windows
```

## JIT position

JIT is an optional VM execution tier, not a separate language mode. A future
386 or modern target may compile hot PBC functions and attach a native entry to
the same stable code reference. The interpreter remains the fallback and the
observable behavior remains identical.

No 8086 JIT is planned initially. Segment allocation, relocation, cache
invalidation and compiler residency would consume the memory the VM is meant to
save.

## Native backend and JIT sharing

Assembly output and future direct machine-code emission must share one target
lowering rather than duplicate instruction selection. The intended pipeline
is:

```text
PIR -> target lowering -> Machine IR
                             |      |
                             |      +-> binary encoder / JIT / object writer
                             +--------> textual assembly printer
```

Machine IR owns physical registers, instruction forms, stack frames,
relocations and ABI operations. The assembly printer only renders those
decisions; the binary encoder only encodes them.

Target selection is decomposed into architecture, platform, ABI, object
format and data layout. The future `TargetDescriptor` records pointer and word
width, integer data model, byte order, alignment, calling convention, stack
rules, relocation kinds and unwind contract. Architecture is never used as a
proxy for ABI: x86-64 SysV and Windows, or big-endian and little-endian
PowerPC64 variants, remain distinct target descriptions. PBC, PIR and Python
string semantics do not depend on the host byte order or native character set.

## Build quality gate

Warnings are errors. Host C/C++ builds use `-Werror`; Open Watcom C/C++ and
assembler invocations use `-we`. A phase cannot pass while a relevant host,
8086 or 386 component emits a warning.

## Implementation phases and acceptance criteria

### Phase 1: execution policy and specification

- formal `auto`, `native`, `vm` and `hybrid` compiler modes;
- native remains byte-for-byte equivalent apart from comments or diagnostics;
- unavailable modes fail before compilation;
- PBC container, code-reference and VM contracts are documented;
- focused compiler CLI tests pass.

### Phase 2: PBC container and verifier

- endian-independent reader and writer;
- bounded section directory and checked arithmetic;
- round-trip tests for every record type;
- malformed and truncated input tests;
- deterministic binary output;
- no runtime execution yet.

### Phase 3: minimal host VM

- constants, locals, globals, branches, calls and return;
- arithmetic through existing runtime primitives;
- explicit pending-exception checks;
- CPython expected output for focused programs;
- differential native versus VM tests on the host.

Status: completed. Focused tests cover arithmetic, locals, globals, branches,
native calls, PBC-to-PBC calls, collections, iteration, exceptions and invalid
frames. The differential arithmetic path is checked against the native runtime
primitive, and the same sources compile warning-free with Open Watcom for 8086
and 386.

### Phase 4: complete Python execution state

- classes and stable method references;
- closures and cells;
- generators and coroutines using bytecode offsets;
- context managers and exception regions;
- reflection metadata;
- GC traversal for VM-owned references.

Status: completed. General vtable methods retain stable native or PBC code
references while native dunder slots preserve indexed dispatch. Focused tests
cover binding and invoking a PBC method, closure cells that outlive their outer
frame, `yield`, `send`, return values carried by `StopIteration`, caught and
uncaught `throw`, clean and rejected `close`, coroutine scheduling, persistent
closure ownership and collection of cycles released by a suspended frame.
Exception regions are emitted from PIR exceptional edges, handlers receive the
captured exception on their verified entry stack, typed matching uses the
runtime exception hierarchy, and unmatched handlers preserve the active
exception for propagation. The focused VM suite currently has 20 passing
tests. The active exception lives in the executing C frame instead of
`PyDosObj`, avoiding a size increase for every object in the 8086 heap.

### Phase 5: external application and stdlib archives

- separate `APP.PBC` and `PYDOS.PYL` generation;
- module graph and initialization states;
- import resolution and cyclic-import behavior;
- per-function reachability retained for native packages;
- external archive version mismatch is diagnosed.

### Phase 6: DOS integration

- 386 VM first for easier diagnostics and larger address space;
- 8086 VM with bounded frames and cache;
- short-timeout execution in the existing DOS test runner;
- memory peak and executable-size measurements;
- no regression in native mode.

### Phase 7: hybrid execution

- one callable gateway for native, builtin and PBC functions;
- explicit compiler policy per module and function;
- active frames pin bytecode pages;
- vtables and reflection survive page eviction;
- native and VM exception propagation are identical.

### Phase 8: additional architectures

- extract target ABI descriptions from backend implementation details;
- add portable platform and object-layout tests;
- introduce x64, ARM and PowerPC backends independently;
- treat pointer width, alignment and byte order as target data;
- evaluate JIT only after VM semantics and verifier are stable.

## Validation matrix

Every phase is tested in these dimensions:

| Dimension | Required checks |
| --- | --- |
| frontend | same AST, sema and optimized PIR for all modes |
| format | round-trip, truncation, corruption, limits, deterministic bytes |
| semantics | CPython 3.12 expected output |
| engines | native versus VM differential output |
| DOS | 8086 and 386 execution with short timeout |
| memory | resident image, heap peak, cache peak and allocation failures |
| compatibility | old native command lines continue working |

No phase is complete merely because it emits an artifact. Completion requires
verification, execution where applicable, expected output and failure-path
tests.
