#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
WORK_DIR=$(mktemp -d "${TMPDIR:-/tmp}/pydos-modes.XXXXXX")

cleanup() {
    rm -rf "$WORK_DIR"
}
trap cleanup EXIT

cd "$ROOT_DIR"

bin/pydos tests/hello.py --mode native \
    --stdlib-idx bin/stdlib.idx -o "$WORK_DIR/native.asm" \
    > "$WORK_DIR/native.log"
test -s "$WORK_DIR/native.asm"

bin/pydos tests/hello.py --stdlib-idx bin/stdlib.idx \
    -o "$WORK_DIR/default.asm" > "$WORK_DIR/default.log"
cmp "$WORK_DIR/native.asm" "$WORK_DIR/default.asm"

bin/pydos tests/hello.py --mode auto \
    --stdlib-idx bin/stdlib.idx -o "$WORK_DIR/auto.asm" \
    > "$WORK_DIR/auto.log"
test -s "$WORK_DIR/auto.asm"
cmp "$WORK_DIR/native.asm" "$WORK_DIR/auto.asm"

bin/pydos tests/hello.py --mode auto -v \
    --stdlib-idx bin/stdlib.idx -o "$WORK_DIR/plan.asm" \
    > "$WORK_DIR/plan.log"
grep -q "Execution mode: requested=auto effective=native" \
    "$WORK_DIR/plan.log"

bin/pydos tests/hello.py --mode vm --stdlib-idx bin/stdlib.idx \
    -o "$WORK_DIR/app.pbc" > "$WORK_DIR/vm.log"
test -s "$WORK_DIR/app.pbc"
test "$(od -An -tx1 -N4 "$WORK_DIR/app.pbc" | tr -d ' \n')" = "50594243"

bin/pydos tests/hello.py --mode vm --stdlib-idx bin/stdlib.idx \
    -o "$WORK_DIR/app2.pbc" > "$WORK_DIR/vm2.log"
cmp "$WORK_DIR/app.pbc" "$WORK_DIR/app2.pbc"

if bin/pydos tests/hello.py --mode hybrid -o "$WORK_DIR/hybrid.out" \
        > "$WORK_DIR/hybrid.log" 2>&1; then
    echo "hybrid mode unexpectedly succeeded" >&2
    exit 1
fi
grep -q "Execution mode 'hybrid' is not available" \
    "$WORK_DIR/hybrid.log"

if bin/pydos tests/hello.py --mode invalid -o "$WORK_DIR/invalid.out" \
        > "$WORK_DIR/invalid.log" 2>&1; then
    echo "invalid mode unexpectedly succeeded" >&2
    exit 1
fi
grep -q "Unknown execution mode: invalid" "$WORK_DIR/invalid.log"

echo "compiler execution mode tests passed"
