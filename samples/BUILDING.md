# Building the sample projects on Linux

Build the native compiler and its standard-library index first:

```sh
CCACHE_DISABLE=1 make -f Makefile.mac compiler
```

The provided build script compiles the runtime, compiler output and final
executables for every sample:

```sh
samples/build_dos_linux.sh all
samples/build_dos_linux.sh 8086 alley_cat edit
```

Outputs are placed under `build/samples/<target>/<sample>/`.

Run an interactive TUI sample in DOSEMU2 terminal mode from the repository
root. The `-t` option enables the real terminal renderer, and `-3` is required
by the current setup:

```sh
dosemu -t -3 -K build/samples/8086/tui_demo -E tui_demo.exe
dosemu -t -3 -K build/samples/8086/alley_cat -E alley_cat.exe
dosemu -t -3 -K build/samples/8086/edit -E edit.exe

dosemu -t -3 -K build/samples/386/tui_demo -E tui_demo.exe
dosemu -t -3 -K build/samples/386/alley_cat -E alley_cat.exe
dosemu -t -3 -K build/samples/386/edit -E edit.exe
```

Do not use `-quiet` for these programs. The DOSEMU2 wrapper implements
`-quiet` with its dumb terminal mode, which supports linear standard output
but not cursor positioning, colors or screen redraw. It is appropriate for
automated console tests, not for an interactive TUI. The terminal should have
at least 80 columns and 25 rows.

These programs are interactive and must be checked in the DOSEMU2 window:

- `tui_demo`: Tab changes focus, Enter invokes the selected button, and the
  first button runs a lambda handler;
- `alley_cat`: arrow keys move the cat and Esc exits;
- `edit`: typing and navigation edit the buffer, F2 saves `DOCUMENT.TXT`,
  and F10 or Esc exits.

Compile a sample by adding its project directory to the module search path:

```sh
bin/pydos samples/alley_cat/main.py \
  --search-path samples/alley_cat \
  --stdlib-idx bin/stdlib.idx \
  -o build/alley_cat.asm
```

Use `-t 386` for the protected-mode build. Without it, the compiler emits
8086 assembly. Assemble and link with the same OpenWatcom runtime and linker
commands documented for the integration tests. The complete, executable
reference for those commands is `tests/run_dos_linux.sh`.

Each project has a `main.py`; helper modules live beside it and are linked
from `from ... import ...` statements. Current samples are:

- `hello_project`: small multi-file console application;
- `tui_demo`: widgets, focus, buttons and lambda event handlers;
- `alley_cat`: real-time text-mode game;
- `edit`: interactive text editor that opens and saves `DOCUMENT.TXT`.

The TUI assumes the standard DOS 80x25 color text mode. Extended keys are
reported as `256 + BIOS scan code`, which keeps ASCII and special keys in a
single integer API while remaining inexpensive on 8086.
