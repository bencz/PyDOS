<p align="center">
  <img width="800" height="600" alt="PyDOS - Python for DOS" src="https://github.com/user-attachments/assets/a660f536-378b-4567-ba35-f79e0c669dcc" />
</p>

**PyDOS compiles a growing Python 3.12+ language subset into standalone DOS executables for the 8086 and 386.**

PyDOS compiles Python through a 9-phase SSA-based pipeline into Open Watcom WASM assembly, links it with a compact C89 runtime, and produces a `.EXE` that runs without an interpreter, VM, Python installation, or generated C source. High-level builtins and library classes are written in Python and merged into the program as PIR; C is reserved for object representation, primitive operations, memory management, dispatch, and DOS/BIOS access.

Two target architectures are supported from the same source with no code changes:

| Target | Mode | Pointers | Extender | Memory |
|--------|------|----------|----------|--------|
| **8086** (default) | 16-bit real mode | 4-byte `segment:offset` (far) | None | 640 KB conventional |
| **386** | 32-bit protected mode | 4-byte linear (flat) | CauseWay | Extended memory |

Current validated baseline:

| Validation | Result |
|---|---:|
| Native C runtime tests | **577/577** |
| DOS integration tests: 8086 | **178/178** |
| DOS integration tests: 386 | **178/178** |

Both DOS suites compile, assemble, link, execute under DOSEMU2, and compare their output byte-for-byte with the expected result.

## Screenshots

| Code | Execution |
|------|-----------|
| <img src="https://github.com/user-attachments/assets/46824568-c713-458e-a287-c81e446f02c7" width="100%"/> | <img src="https://github.com/user-attachments/assets/d282b62b-b277-4f5b-9d04-ca5c1f97d8e0" width="100%"/> |
| <img width="737" height="409" alt="image" src="https://github.com/user-attachments/assets/87f970a8-d39d-4161-bc02-88ab0c427d2b" /> | <img width="740" height="404" alt="image" src="https://github.com/user-attachments/assets/a621199c-915c-4f4e-ab96-f01a71ffcfb3" /> |

## TUI applications

PyDOS includes a Pythonic text user interface library under `pydos.io.tui`. The DOS runtime provides only the low-level BIOS/DOS primitives for screen, keyboard, clock, and file access. Canvas composition, screens, widgets, buttons, handlers, lambdas, dialogs, menus, focus management, and event loops remain ordinary compiled Python.

The sample projects include a TUI demo, an Alley Cat-inspired game, and a full-screen EDIT-style text editor with document buffers, menus, dialogs, search/replace, navigation, save/open operations, and keyboard shortcuts. They build for both 8086 real mode and 386 protected mode.

Build and run the EDIT sample on Linux:

```bash
samples/build_dos_linux.sh 8086 edit
dosemu -t -3 -K build/samples/8086/edit -E edit.exe
```

Use `samples/build_dos_linux.sh 386 edit` and `build/samples/386/edit` for the protected-mode build.

Source code: [`samples/edit`](samples/edit)

<img width="785" height="611" alt="image" src="https://github.com/user-attachments/assets/cfe48f95-1c50-43bf-8036-3ec60c3d17f7" />

## Language Features

Type annotations are supported and improve specialization, but ordinary unannotated Python functions are also compiled. PyDOS aims at Python 3.12+ semantics; the list below describes features already exercised by the current test suite rather than claiming complete CPython compatibility.

### Functions, recursion, and default parameters

```python
def factorial(n: int) -> int:
    if n <= 1:
        return 1
    return n * factorial(n - 1)

for i in range(1, 10):
    print(factorial(i))
```

### Classes, single and multiple inheritance

```python
class Animal:
    def __init__(self, name: str, sound: str) -> None:
        self.name = name
        self.sound = sound

    def speak(self) -> str:
        return self.name + " says " + self.sound

class Dog(Animal):
    def __init__(self, name: str) -> None:
        super().__init__(name, "Woof")

    def fetch(self, item: str) -> str:
        return self.name + " fetches " + item

dog: Dog = Dog("Rex")
print(dog.speak())       # Rex says Woof
print(dog.fetch("ball")) # Rex fetches ball
```

Multiple inheritance works too:

```python
class Flyable:
    def fly(self) -> str:
        return "flying"

class Swimmable:
    def swim(self) -> str:
        return "swimming"

class Duck(Flyable, Swimmable):
    def quack(self) -> str:
        return "quack"

d: Duck = Duck()
print(d.fly())   # flying
print(d.swim())  # swimming
print(d.quack()) # quack
```

### Python 3.12 object model

The object model supports C3 MRO, multiple inheritance, bound methods, runtime class identity, `type`, `object`, `super`, class and instance `__dict__`, dynamic attribute replacement, and guarded vtable fast paths. Materialized values in the class dictionary remain authoritative, so replacing a compiled method at runtime preserves Python lookup semantics.

Descriptors include `property`, `classmethod`, and `staticmethod`, including abstract properties and setter/deleter chains. User metaclasses support `metaclass=`, inherited metaclasses, conflict detection, `__prepare__`, custom namespace mappings, `__new__`, `__init__`, class keywords, and custom `__subclasshook__` behavior.

`abc` provides `ABCMeta`, `ABC`, `abstractmethod`, virtual subclass registration, cache tokens, and integration with descriptors. `dataclasses` provides the compiled `@dataclass` path used by the matrix sample.

```python
from abc import ABC, abstractmethod
from dataclasses import dataclass

class Shape(ABC):
    @abstractmethod
    def area(self):
        pass

@dataclass
class Rectangle(Shape):
    width: int
    height: int

    def area(self):
        return self.width * self.height

print(Rectangle(4, 3).area())  # 12
```

### Operator overloading (73 dunder slots)

PyDOS supports 73 dunder method slots with O(1) vtable dispatch - `__add__`, `__sub__`, `__mul__`, `__matmul__`, `__eq__`, `__lt__`, `__hash__`, `__iter__`, `__next__`, `__call__`, `__enter__`/`__exit__`, `__getattr__`, `__contains__`, `__len__`, `__neg__`, `__pos__`, `__abs__`, `__invert__`, `__iadd__`, `__radd__`, and many more:

```python
class Vec2:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y
    def __add__(self, other: Vec2) -> Vec2:
        return Vec2(self.x + other.x, self.y + other.y)
    def __str__(self) -> str:
        return str(self.x) + "," + str(self.y)

a: Vec2 = Vec2(3, 4)
b: Vec2 = Vec2(1, 2)
print(a + b)  # 4,6
```

Callable objects via `__call__`:

```python
class Adder:
    def __init__(self, base: int) -> None:
        self.base = base
    def __call__(self, x: int) -> int:
        return self.base + x

add5: Adder = Adder(5)
print(add5(3))   # 8
print(add5(10))  # 15
```

Custom iterators via `__iter__` / `__next__`:

```python
class Counter:
    def __init__(self, start: int, stop: int) -> None:
        self.current = start
        self.stop = stop
    def __iter__(self) -> Counter:
        return self
    def __next__(self) -> int:
        if self.current >= self.stop:
            raise StopIteration()
        val: int = self.current
        self.current = self.current + 1
        return val

for x in Counter(1, 5):
    print(x)  # 1 2 3 4
```

### Generics with monomorphization

Generic classes use type parameters. The compiler monomorphizes them - `Stack[int]` and `Stack[str]` become separate specialized implementations at compile time:

```python
class Stack[T]:
    def __init__(self) -> None:
        self.items: list[T] = []
        self.size: int = 0

    def push(self, item: T) -> None:
        self.items.append(item)
        self.size = self.size + 1

    def pop(self) -> T:
        self.size = self.size - 1
        return self.items.pop(self.size)

    def peek(self) -> T:
        return self.items[self.size - 1]

int_stack: Stack[int] = Stack[int]()
int_stack.push(10)
int_stack.push(20)
print(int_stack.pop())  # 20

str_stack: Stack[str] = Stack[str]()
str_stack.push("hello")
str_stack.push("world")
print(str_stack.pop())  # world
```

Multi-parameter generics:

```python
class Pair[T]:
    def __init__(self, first: T, second: T) -> None:
        self.first = first
        self.second = second
    def __add__(self, other: Pair[T]) -> Pair[T]:
        return Pair(self.first + other.first, self.second + other.second)
    def __str__(self) -> str:
        return str(self.first) + "," + str(self.second)

a: Pair[int] = Pair(3, 4)
b: Pair[int] = Pair(1, 2)
print(a + b)  # 4,6
```

### Generators - `yield`, `yield from`, `send`, `throw`, `close`

```python
def fibonacci(limit: int) -> object:
    a: int = 0
    b: int = 1
    while a < limit:
        yield a
        temp: int = a + b
        a = b
        b = temp

for n in fibonacci(100):
    print(n)  # 0 1 1 2 3 5 8 13 21 34 55 89
```

Full generator protocol with `send()`:

```python
def accumulator() -> object:
    total: int = 0
    while True:
        val: object = yield total
        if val is None:
            break
        total = total + val

a: object = accumulator()
next(a)             # prime the generator
print(a.send(10))   # 10
print(a.send(20))   # 30
print(a.send(5))    # 35
```

Delegation with `yield from`:

```python
def inner() -> object:
    yield 1
    yield 2

def outer() -> object:
    yield from inner()
    yield 3

for x in outer():
    print(x)  # 1 2 3
```

Generator expressions:

```python
result: list = list(x * 2 for x in range(5))
# [0, 2, 4, 6, 8]
```

### Async / await with cooperative scheduling

```python
async def add(a: int, b: int) -> int:
    return a + b

async def main() -> None:
    x: int = await add(10, 20)
    print(x)  # 30

print(async_run(main()))
```

`async_gather` for concurrent coroutine execution with round-robin scheduling:

```python
async def worker(name: str, n: int) -> str:
    i: int = 0
    while i < n:
        print(name + " " + str(i))
        await None
        i = i + 1
    return name + " done"

async def main() -> None:
    results: list = async_gather([worker("A", 3), worker("B", 2)])
    for x in results:
        print(x)

async_run(main())
# A 0 -> B 0 -> A 1 -> B 1 -> A 2 -> A done -> B done
```

### Lambda and first-class functions

Functions are first-class values - store them in variables, pass them as arguments, put them in dicts:

```python
sq: object = lambda x: x * x
print(sq(5))  # 25

def apply(f: object, x: int) -> int:
    return f(x)

print(apply(lambda x: x + 10, 7))  # 17

def double(x: int) -> int:
    return x * 2

# Store functions in a dict
ops: dict = {"dbl": double, "sq": sq}
f: object = ops["dbl"]
print(f(50))  # 100
```

### Closures and `nonlocal`

```python
def outer() -> None:
    x: int = 10
    def inner() -> None:
        nonlocal x
        x = x + 1
    inner()
    print(x)  # 11
    inner()
    print(x)  # 12

outer()
```

### Match / case (structural pattern matching)

Literal, capture, wildcard, OR, guard, sequence, mapping, and class patterns:

```python
def describe(val) -> str:
    match val:
        case 1:
            return "one"
        case 2 | 3:
            return "two or three"
        case n if n < 0:
            return "negative"
        case _:
            return "other"
```

Class patterns with attribute binding:

```python
class Point:
    def __init__(self, x: int, y: int) -> None:
        self.x = x
        self.y = y

def check(s) -> None:
    match s:
        case Point(x=0, y=0):
            print("origin")
        case Point(x=a, y=b):
            print(a, b)
```

Star-in-sequence patterns:

```python
match [1, 2, 3, 4, 5]:
    case [first, *middle, last]:
        print(first)   # 1
        print(middle)  # [2, 3, 4]
        print(last)    # 5
```

### Exception handling - `try`/`except`/`finally`, `raise`, `except*`

```python
try:
    x: int = int("not a number")
except ValueError:
    print("bad value")
finally:
    print("always runs")
```

20+ built-in exception types with inheritance-based matching (e.g. `except LookupError` catches both `KeyError` and `IndexError`).

Exception groups (PEP 654):

```python
try:
    raise ExceptionGroup("errors", [ValueError("bad"), TypeError("wrong")])
except* ValueError as eg:
    print("caught ValueError group")
except* TypeError as eg:
    print("caught TypeError group")
```

Exceptions use explicit pending-state propagation through PIR control-flow edges. `finally`, context-manager cleanup, arena release, iterator failures, return from `try`, and exceptions raised inside cleanup are represented in the CFG; the runtime does not use `setjmp`/`longjmp`.

### Context managers (`with` statement)

```python
class CtxMgr:
    def __init__(self, name: str) -> None:
        self.name = name
    def __enter__(self) -> str:
        print("enter " + self.name)
        return self.name
    def __exit__(self, exc_type, exc_val, tb) -> bool:
        print("exit " + self.name)
        return False

with CtxMgr("A") as val:
    print("body " + val)
# enter A -> body A -> exit A
```

### Comprehensions - list, dict, set

```python
evens: list = [x for x in range(10) if x % 2 == 0]
# [0, 2, 4, 6, 8]

squares: dict = {x: x * x for x in range(5)}
# {0: 0, 1: 1, 2: 4, 3: 9, 4: 16}

unique: set = {x % 3 for x in range(10)}
# {0, 1, 2}
```

### F-strings

```python
name: str = "DOS"
x: int = 42
print(f"Hello, {name}! The answer is {x}.")
print(f"{3} + {4} = {3 + 4}")
```

### `*args` and `**kwargs`

```python
def sum_all(first: int, *rest: int) -> int:
    result: int = first
    i: int = 0
    while i < len(rest):
        result = result + rest[i]
        i = i + 1
    return result

print(sum_all(1, 2, 3, 4))  # 10
```

### Positional-only parameters (PEP 570)

```python
def add(a: int, b: int, /) -> int:
    return a + b

print(add(1, 2))  # 3
```

### Star unpacking (PEP 3132)

```python
a: int
b: list
c: int
a, *b, c = [1, 2, 3, 4, 5]
print(a)  # 1
print(b)  # [2, 3, 4]
print(c)  # 5
```

### Walrus operator (`:=`)

```python
if (n := 10) > 5:
    print(n)  # 10
```

### Type aliases (PEP 695)

```python
type Vector = list[float]
type Matrix = list[Vector]
```

### Slicing and negative indexing

```python
s: str = "Hello, World!"
print(s[0:5])    # Hello
print(s[-6:-1])  # World

nums: list = [10, 20, 30, 40, 50]
print(nums[1:4])    # [20, 30, 40]
print(nums[::2])    # [10, 30, 50]
```

### Collections

Lists, dicts, sets, tuples, frozensets, bytes, bytearrays, ranges, and complex numbers are supported. Frequently used Python 3.12 methods are implemented; some less common methods and edge cases are still being completed.

```python
# Lists - append, pop, insert, sort, reverse, index, remove, copy, slicing
bag: list = [3, 1, 4, 1, 5]
bag.sort()
print(bag)  # [1, 1, 3, 4, 5]

# Dicts - get, pop, update, copy, keys/values/items, setdefault, in
inventory: dict = {"sword": 1, "potion": 5}
print(inventory.get("potion", 0))  # 5

# Sets - add, remove, discard, union, intersection, difference
s: set = {1, 2, 3}
s.add(4)
print(s & {2, 3, 5})  # {2, 3}

# Tuples
t: tuple = (1, "hello", True)
print(t[1])  # hello

# Frozensets (immutable sets)
fs = frozenset([3, 1, 2, 1])
print(len(fs))     # 3
print(1 in fs)     # True

# Complex numbers (j suffix)
c = (1+2j) * (3+4j)
print(c)  # (-5+10j)

# Bytearrays (mutable byte sequences)
ba = bytearray([72, 101, 108])
ba.append(108)
ba.append(111)
print(len(ba))  # 5

# Bytes (immutable byte sequences)
raw = bytes([80, 121, 68, 79, 83])
print(raw[0])       # 80
print(raw[1:3])     # b'yD'
```

### Builtins

The current suite exercises `abs`, `all`, `any`, `bin`, `bool`, `bytearray`, `bytes`, `callable`, `chr`, `complex`, `delattr`, `dict`, `divmod`, `enumerate`, `filter`, `float`, `frozenset`, `getattr`, `hasattr`, `hash`, `hex`, `id`, `input`, `int`, `isinstance`, `issubclass`, `iter`, `len`, `list`, `map`, `max`, `min`, `next`, `object`, `oct`, `open`, `ord`, `pow`, `print`, `range`, `repr`, `reversed`, `set`, `setattr`, `sorted`, `str`, `sum`, `super`, `tuple`, `type`, `vars`, and `zip`.

Several of these intentionally implement a practical DOS subset today. Name availability alone is not considered complete Python compatibility.

### `del` statement

```python
x: int = 10
del x           # delete variable
del obj.attr    # delete attribute
del lst[2]      # delete by index
```

### Multi-file modules

```python
# mod_hlp.py
def add(a: int, b: int) -> int:
    return a + b

class Counter:
    def __init__(self, start: int) -> None:
        self.value = start
    def inc(self) -> None:
        self.value = self.value + 1
```

```python
# mod_ent.py
from mod_hlp import add, Counter

def main() -> None:
    print(add(10, 20))
    c: Counter = Counter(0)
    c.inc()
    print(c.value)
```

### Pythonic file I/O

`open()` returns a compiled Python stream object with context-manager support, iteration, `read`, `readline`, `write`, and automatic close. Only DOS handle and buffer primitives live in C.

```python
path = "FILE.TXT"

try:
    with open(path, "r", encoding="utf-8") as file:
        for number, line in enumerate(file, start=1):
            print(f"Line {number}: {line.strip()}")
except FileNotFoundError:
    print(f"File '{path}' was not found.")
except OSError as error:
    print(f"I/O error: {error}")
```

The current DOS implementation supports text modes `r`, `w`, and `r+`. Binary streams, full codec support, `seek`/`tell`, and universal-newline semantics remain future work.

### Algorithms that work on real DOS hardware

```python
def is_prime(n: int) -> bool:
    if n < 2:
        return False
    i: int = 2
    while i * i <= n:
        if n % i == 0:
            return False
        i += 1
    return True

def collatz(n: int) -> int:
    steps: int = 0
    while n != 1:
        if n % 2 == 0:
            n = n // 2
        else:
            n = n * 3 + 1
        steps += 1
    return steps

print(collatz(27))  # 111
print(0xFF & 0x0F)  # 15
print(1 << 10)      # 1024
```

## Current compatibility limits

PyDOS targets Python 3.12+, but it is not yet a drop-in replacement for CPython. The most important current limits are:

- `int` uses a signed 32-bit representation; arbitrary-precision integers are not implemented yet;
- `str` storage is currently byte-oriented, so full Unicode code-point and codec semantics are still in progress;
- a single 8086 heap block must fit inside one segment, including its aligned allocator header;
- packages, relative imports, dynamic `__import__`, `eval`, `exec`, and rich frame introspection are incomplete;
- networking, SSL, SQLite, multiprocessing, native threads, subprocesses, and other facilities unavailable or impractical on an 8086 DOS baseline are not part of the core compatibility promise;
- some Python 3.12 signatures, diagnostics, reflection details, iterator variants, and standard-library modules remain under active development.

The compiler rejects or exposes unsupported behavior rather than intentionally emulating modern operating-system services in the minimal C runtime.

## Prerequisites

[Open Watcom v2](https://github.com/open-watcom/open-watcom-v2) is required to assemble and link DOS executables. The repository supports a portable local toolchain at `toolchains/openwatcom/distribution`, so a system-wide Watcom installation is not required.

Required host tools:

- GCC/G++ or Clang and GNU Make for the native compiler and runtime tests;
- Open Watcom tools: `wcc`, `wcc386`, `wasm`, `wlink`, and `wlib`;
- DOSEMU2 for automated 8086 and 386/CauseWay execution on Linux;
- `dj64dev-dj64` on Fedora, required by the DOSEMU2 DOS userspace.

Fedora setup for the emulator:

```bash
sudo dnf copr enable stsp/dosemu2
sudo dnf install dosemu2
sudo dnf --refresh install dj64dev-dj64
```

## Building

### Linux host build and full DOS validation

The normal development workflow keeps the compiler native and uses Open Watcom only for DOS objects and executables:

```text
Python source -> native bin/pydos -> WASM assembly -> Open Watcom
              -> DOS executable -> DOSEMU2
```

Build the native compiler and precompiled stdlib index:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac compiler
```

Run all 577 native C runtime tests:

```bash
CCACHE_DISABLE=1 make -f Makefile.mac test
```

Compile, link, run, and compare all 178 integration programs on both targets:

```bash
tests/run_dos_linux.sh all
```

Run selected tests while iterating:

```bash
tests/run_dos_linux.sh all hello matrix editmodel linq
```

The runner automatically discovers the repository-local Open Watcom, constructs both runtime libraries, invokes WASM/WLINK, runs DOSEMU2 with a 386 CPU (`-3`), normalizes DOS CRLF output, and compares it with each `.exp` file.

### Full build (everything, both architectures)

```
wmake all
```

This builds the compiler, both runtime libraries (16-bit and 32-bit), the pre-compiled stdlib index, and runs all unit tests. Outputs:

| Artifact | Description |
|----------|-------------|
| `bin\PYDOS.EXE` | The compiler (runs under DOS/4GW) |
| `bin\STDLIB.IDX` | Pre-compiled stdlib (builtins, type stubs, PIR functions) |
| `lib\PYDOSRT.LIB` | 16-bit runtime library (8086 real mode) |
| `lib\PDOS32RT.LIB` | 32-bit runtime library (386 protected mode) |

### Individual targets

```
wmake compiler      # Compiler + stdlib index only
wmake runtime       # 16-bit runtime library only
wmake runtime32     # 32-bit runtime library only
wmake test          # Build + run 16-bit unit tests
wmake test32        # Build + run 32-bit unit tests
wmake clean         # Remove all build artifacts
```

### Native host compiler (Linux/macOS development and debugging)

The native compiler generates assembly on Linux and macOS. Linux can also use the bundled Open Watcom host binaries to assemble, link, and execute the DOS programs. On macOS, the native build is primarily useful for compiler tests and assembly/PIR inspection unless a compatible Watcom/emulator environment is provided.

```bash
make -f Makefile.mac compiler    # Build bin/pydos + bin/stdlib.idx
make -f Makefile.mac test        # Build + run C unit tests (~577 tests)
make -f Makefile.mac clean       # Clean
```

## Compiling a Python Program

Use `pydc.bat` to compile a `.py` file into a standalone `.EXE`:

```
pydc.bat input.py outputname           REM 16-bit (8086 real mode)
pydc.bat input.py outputname 386       REM 32-bit (386, CauseWay extender)
```

This runs the full pipeline: **Python source -> compiler -> .ASM -> WASM assembler -> .OBJ -> WLINK linker -> .EXE**.

Example:

```
C:\PYDOS> pydc.bat tests\hello.py hello
PyDOS Compiler (8086): tests\hello.py -> hello.EXE
Success: hello.EXE created.

C:\PYDOS> hello.EXE
Hello, DOS!
```

32-bit mode:

```
C:\PYDOS> pydc.bat tests\fact.py fact 386
PyDOS Compiler (386): tests\fact.py -> fact.EXE
Success: fact.EXE created (CauseWay).

C:\PYDOS> fact.EXE
1
2
6
24
120
720
5040
40320
362880
```

### Native compiler diagnostics

```bash
./bin/pydos tests/hello.py -o hello.asm                    # 8086 assembly
./bin/pydos tests/hello.py -o hello.asm -t 386             # 386 assembly
./bin/pydos tests/hello.py -o hello.asm --dump-pir         # Print PIR and exit
./bin/pydos tests/hello.py -o hello.asm --dump-types       # Print inferred types
./bin/pydos tests/hello.py -o hello.asm --dump-escape      # Print escape analysis
./bin/pydos tests/hello.py -o hello.asm -v                 # Dump tokens, AST, PIR, and IR
./bin/pydos tests/hello.py -o hello.asm --no-pir-opt       # Skip all PIR optimization
./bin/pydos tests/hello.py -o hello.asm --no-sccp --no-gvn # Disable selected passes
./bin/pydos tests/hello.py -o hello.asm --no-scope         # Disable arena-scope insertion
```

Module builds can additionally use `-m`, `-M`, `-L`, `--entry`, `--search-path`, and `--stdlib-idx`. Running `bin/pydos --help` prints the authoritative option list for the current compiler.

## Running Tests

### Integration tests (DOS)

The DOS integration suite currently contains 178 enabled Python programs in `tests/`, normally paired with a `.exp` expected-output file.

**Run the full suite:**

```
runtests.bat           REM 16-bit
runtests.bat 386       REM 32-bit
```

**Run a single test:**

```
runone.bat hello           REM Compiles tests\hello.py, runs the EXE, compares stdout to tests\hello.exp
runone.bat fact 386        REM Same, 32-bit
```

`runone.bat` performs the full cycle: compile -> assemble -> link -> execute -> compare stdout against `.exp`. Prints `PASS` on exact match, `FAIL` with a diff on mismatch.

`runtests.bat` runs all tests sequentially and prints a summary:

```
=== Running tests in 8086 mode ===
PASS hello
PASS arith
PASS cls_bas
PASS gen_stk
...

=== Passed ===
hello
arith
cls_bas
...

=== Failed ===
(none)
```

### Host-side assembly checks

When DOS execution is unavailable, the host-side script can still verify that the compiler successfully generates assembly for the suite:

```bash
tests/run_mac.sh                 # All tests, with PIR optimization
tests/run_mac.sh --no-pir-opt    # All tests, without optimization
```

### Unit tests

The 27 native C-level suites currently contain 577 tests. They exercise object creation, memory allocation, GC cycles, string operations, integer arithmetic, list/dict/set operations, vtable dispatch, exception handling, generators, closures, and end-to-end integration:

```
wmake test          REM 16-bit
wmake test32        REM 32-bit
```

On a Linux or macOS host:

```bash
make -f Makefile.mac test
```

## How It Works

The compiler reads a `.py` file and transforms it through nine phases:

**Lexer** -> **Parser** -> **Sema** -> **Mono** -> **PIR Builder** -> **PIR Optimizer** -> **PIR Lowerer** -> **IR Optimizer** -> **Codegen**

The PIR optimizer performs type inference, escape analysis, SCCP, mem2reg, GVN, LICM, dead-code and dead-block elimination, devirtualization, specialization, arena-scope insertion, and related lowering work before the dual 8086/386 backends generate Watcom assembly. Individual passes can be inspected or disabled from the command line.

Runtime values use the tagged `PyDosObj` representation. Heap objects are reference counted, container cycles are handled by a mark-and-sweep collector, frequently used immutable values may be shared, and temporary ownership is grouped in compiler-inserted arena scopes. On 8086, object references are four-byte far pointers and generated procedures are split into independent `PYCODE<n>` code segments so that a large program is not forced into one 64 KiB code segment.

Exceptions do not depend on `setjmp` or `longjmp`. Runtime primitives store a pending exception, generated control flow checks and propagates that state through cleanup blocks, and `finally`, context-manager exits, returns, breaks, and continues are lowered explicitly. This keeps unwinding visible to the optimizer and avoids copying opaque C stack environments on memory-constrained DOS targets.

The C89 runtime is deliberately limited to performance-critical primitives: allocation, tagged values, primitive numeric and string operations, reference ownership, exception state, and BIOS/DOS services. Common builtins, collection behavior, descriptors, ABCs, dataclasses, file streams, TUI widgets, and other high-level behavior are implemented in Python or compiled PIR and optimized together with user code.

The compiler is written in **C++98** without the STL, and the runtime in **C89**. Both remain compatible with Open Watcom 2.

## Example Output

`print("Hello, DOS!")` compiles to this 8086 WASM assembly:

```asm
; PyDOS compiler output - 8086 WASM assembly
.8086
.MODEL LARGE

.DATA

_SC0 db "Hello, DOS!", 0
_SC0_LEN equ 11

.CODE

EXTRN pydos_rt_init_:FAR
EXTRN pydos_rt_shutdown_:FAR
EXTRN pydos_arena_scope_enter_:FAR
EXTRN pydos_obj_new_str_:FAR
EXTRN pydos_arena_scope_track_:FAR
EXTRN pydos_builtin_print_:FAR
EXTRN pydos_obj_call_:FAR
EXTRN pydos_arena_scope_track_ref_:FAR
EXTRN pydos_exc_pending_:FAR
EXTRN pydos_arena_scope_exit_:FAR
EXTRN pydos_obj_new_none_:FAR
EXTRN pydos_exc_panic_current_:FAR

.CODE PYCODE0
PUBLIC _hello_x2e_s_sinit_s_s
_hello_x2e_s_sinit_s_s PROC FAR
    push bp
    mov  bp, sp
    push si
    push di
    sub  sp, 16
    ; zero-init locals and temps
    push es
    push ss
    pop  es
    lea  di, [bp-20]
    mov  cx, 8
    xor  ax, ax
    cld
    rep  stosw
    pop  es

_L0:
    ; SCOPE_ENTER
    call far ptr pydos_arena_scope_enter_
    push ss
    pop  ds
    ; CONST_STR len=11 -> t0
    mov  ax, 11
    push ax
    mov  ax, seg _SC0
    push ax
    mov  ax, offset _SC0
    push ax
    push ss
    pop  ds
    call far ptr pydos_obj_new_str_
    add  sp, 6
    push ss
    pop  ds
    mov  word ptr [bp-8], ax
    mov  word ptr [bp-6], dx
    ; SCOPE_TRACK t0
    push word ptr [bp-6]
    push word ptr [bp-8]
    call far ptr pydos_arena_scope_track_
    add  sp, 4
    push ss
    pop  ds
    ; LOAD_GLOBAL (builtin) 'print' -> t2 (skip)
    ; CALL t2(1 args) -> t1
    ; builtin: print (1 args)
    sub  sp, 4
    mov  si, sp
    mov  ax, word ptr [bp-8]
    mov  word ptr ss:[si+0], ax
    mov  ax, word ptr [bp-6]
    mov  word ptr ss:[si+2], ax
    push ss
    push si
    mov  ax, 1
    push ax
    push ss
    pop  ds
    call far ptr pydos_builtin_print_
    add  sp, 10
    push ss
    pop  ds
    mov  word ptr [bp-12], ax
    mov  word ptr [bp-10], dx
    ; SCOPE_TRACK t1
    push word ptr [bp-10]
    push word ptr [bp-12]
    call far ptr pydos_arena_scope_track_ref_
    add  sp, 4
    push ss
    pop  ds
    ; CHECK_EXCEPTION -> _L1
    push ss
    pop  ds
    call far ptr pydos_exc_pending_
    push ss
    pop  ds
    test ax, ax
    jnz  _L1
    ; SCOPE_EXIT
    call far ptr pydos_arena_scope_exit_
    push ss
    pop  ds
    ; CONST_NONE -> t3
    push ss
    pop  ds
    call far ptr pydos_obj_new_none_
    push ss
    pop  ds
    mov  word ptr [bp-20], ax
    mov  word ptr [bp-18], dx
    ; RETURN t3
    mov  ax, word ptr [bp-20]
    mov  dx, word ptr [bp-18]
    jmp  ___init___epilogue
_L1:
    ; PANIC_EXCEPTION
    push ss
    pop  ds
    call far ptr pydos_exc_panic_current_
    push ss
    pop  ds
    ; SCOPE_EXIT
    call far ptr pydos_arena_scope_exit_
    push ss
    pop  ds
    ; RETURN void
    xor  ax, ax
    xor  dx, dx
    jmp  ___init___epilogue
___init___epilogue:
    add  sp, 16
    pop  di
    pop  si
    mov  sp, bp
    pop  bp
    retf
_hello_x2e_s_sinit_s_s ENDP

.CODE PYCODE1
PUBLIC main_
main_ PROC FAR
    push bp
    mov  bp, sp

    ; init runtime
    push ss
    pop  ds
    call far ptr pydos_rt_init_

    ; call __init__
    push ss
    pop  ds
    call far ptr _hello_x2e_s_sinit_s_s

    ; shutdown runtime
    push ss
    pop  ds
    call far ptr pydos_rt_shutdown_

    ; exit to DOS
    mov  ax, 4C00h
    int  21h
main_ ENDP

END
```

On 8086, every Python object reference is a four-byte far pointer (`segment:offset`). The generated code follows Watcom's large-model `__cdecl` convention: arguments are pushed right-to-left, the caller cleans the stack, object references return in `DX:AX`, and `DS` is restored to `DGROUP` after far calls. The current output also shows compiler-inserted arena ownership, explicit pending-exception checks, and separate `PYCODE0`/`PYCODE1` segments.

With `-t 386`, the same source produces flat-model 32-bit assembly with near calls, 32-bit registers, no segment arithmetic, and object references returned in `EAX`.
