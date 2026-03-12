# PyDOS

<p align="center">
  <img width="1536" height="1024" alt="PyDOS - Python for DOS"
  src="https://github.com/user-attachments/assets/e97afe93-6b30-46e3-8774-18a51ec373e5" />
</p>


**PyDOS compiles Python 3.12 into standalone DOS executables.**

Write type-annotated Python and PyDOS compiles it through a 9-phase SSA-based pipeline into Open Watcom WASM assembly, links it with a C89 runtime library, and produces a `.EXE` that runs on bare DOS hardware. No interpreter, no VM, no Python installation required.

Two target architectures are supported from the same source with no code changes:

| Target | Mode | Pointers | Extender | Memory |
|--------|------|----------|----------|--------|
| **8086** (default) | 16-bit real mode | 4-byte `segment:offset` (far) | None | 640 KB conventional |
| **386** | 32-bit protected mode | 4-byte linear (flat) | CauseWay | Extended memory |

## Screenshots

| Code | Execution |
|------|-----------|
| <img src="https://github.com/user-attachments/assets/1d9b54af-98ac-473a-ac09-883d4a0dfbad" width="100%"/> | <img src="https://github.com/user-attachments/assets/11cd6398-726e-4b92-96d8-126c1a6ef94a" width="100%"/> |
| <img width="737" height="409" alt="image" src="https://github.com/user-attachments/assets/77200dd0-93f6-47b8-af55-1baa927fb20f" /> | <img width="740" height="404" alt="image" src="https://github.com/user-attachments/assets/8bca1f60-35e9-429d-a889-d7e2cd8eadf0" /> |

## Language Features

All function parameters and return types require type annotations. This is the only syntactic constraint - everything else is standard Python 3.12.

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
# A 0 → B 0 → A 1 → B 1 → A 2 → A done → B done
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
# enter A → body A → exit A
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

Lists, dicts, sets, tuples, frozensets, bytearrays, and complex numbers are all supported with their standard methods:

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
```

### Builtins

`print`, `input`, `len`, `range`, `type`, `isinstance`, `issubclass`, `int`, `str`, `bool`, `float`, `abs`, `min`, `max`, `ord`, `chr`, `hex`, `sorted`, `reversed`, `enumerate`, `zip`, `map`, `filter`, `any`, `all`, `sum`, `list`, `dict`, `set`, `tuple`, `frozenset`, `complex`, `bytearray`, `next`, `hash`.

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

## Prerequisites

[Open Watcom v2](https://github.com/open-watcom/open-watcom-v2) must be installed. Ensure:

- `WATCOM` environment variable points to your Watcom installation
- Watcom tools are in your `PATH`: `wmake`, `wpp386`, `wcc`, `wcc386`, `wasm`, `wlink`, `wlib`

## Building

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

### macOS (development / debugging)

The compiler can be built as a native macOS binary for development. It generates assembly output but cannot assemble or link (that requires Watcom on DOS).

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

This runs the full pipeline: **Python source → compiler → .ASM → WASM assembler → .OBJ → WLINK linker → .EXE**.

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
Success: fact.EXE created (DOS/4GW).

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

### On macOS (assembly inspection only)

```bash
./bin/pydos tests/hello.py -o hello.asm             # 8086 assembly
./bin/pydos tests/hello.py -o hello.asm -t 386       # 386 assembly
./bin/pydos tests/hello.py -o hello.asm --dump-pir   # Print SSA IR and exit
./bin/pydos tests/hello.py -o hello.asm --no-pir-opt # Skip PIR optimization
```

## Running Tests

### Integration tests (DOS)

151 Python test programs in `tests/`, each with a `.py` source and a `.exp` expected output file.

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

`runone.bat` performs the full cycle: compile → assemble → link → execute → compare stdout against `.exp`. Prints `PASS` on exact match, `FAIL` with a diff on mismatch.

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

### Integration tests (macOS)

On macOS, tests verify successful assembly generation (cannot execute):

```bash
tests/run_mac.sh                 # All tests, with PIR optimization
tests/run_mac.sh --no-pir-opt    # All tests, without optimization
```

### Unit tests

27 C-level test suites (~577 tests) exercise the runtime library directly - object creation, memory allocation, GC cycles, string operations, integer arithmetic, list/dict/set operations, vtable dispatch, exception handling, generators, closures, and end-to-end integration:

```
wmake test          REM 16-bit
wmake test32        REM 32-bit
```

On macOS:

```bash
make -f Makefile.mac test
```

## How It Works

The compiler reads a `.py` file and transforms it through 9 phases:

**Lexer** → **Parser** → **Sema** → **Mono** → **PIR Builder** → **PIR Optimizer** (13 passes: SCCP, mem2reg, GVN, LICM, type inference, escape analysis, devirtualization, type specialization, ...) → **PIR Lowerer** → **IR Optimizer** → **Codegen** (dual 8086/386 backends)

Every Python value at runtime is a heap-allocated object (`PyDosObj`) with a type tag, managed by reference counting with a mark-and-sweep cycle collector for container cycles. The runtime provides the object model, memory management, garbage collection, exception handling via `setjmp`/`longjmp`, vtable dispatch with 73 dunder slots, and DOS I/O via INT 21h.

Common builtins and collection methods (`any`, `all`, `sum`, `min`, `max`, `sorted`, `reversed`, `enumerate`, `zip`, `map`, `filter`, and more) are written in Python, pre-compiled to PIR at build time, and merged into user programs during compilation - they get optimized alongside your code.

The compiler is written in **C++98** (no STL), the runtime in **C89**. Both compile under Open Watcom 2.

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
EXTRN pydos_obj_new_str_:FAR
EXTRN pydos_builtin_print_:FAR
EXTRN pydos_obj_new_none_:FAR

PUBLIC _HELLO____init__
_HELLO____init__ PROC FAR
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
    ; CONST_STR "Hello, DOS!" -> t0
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
___init___epilogue:
    add  sp, 16
    pop  di
    pop  si
    mov  sp, bp
    pop  bp
    retf
_HELLO____init__ ENDP

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
    call far ptr _HELLO____init__

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

Every Python value is a 4-byte far pointer (segment:offset) to a runtime-allocated object. The generated code follows Watcom large-model `__cdecl` calling convention: arguments pushed right-to-left, caller cleans the stack, return value in DX:AX, DS restored to DGROUP after every far call.

With `-t 386`, the same source produces flat-model 32-bit assembly: near calls, 32-bit registers (EAX/EBX/ECX/EDX), no segment arithmetic, return value in EAX alone.
