#!/bin/bash
set -uo pipefail

LHD=./bazel-bin/lhd/lhd
if [[ ! -x "$LHD" ]]; then
  LHD=./lhd/lhd
fi

GOOD=${1:?missing good Pyrope source}
BAD=${2:?missing bad Pyrope source}
WD="${TEST_TMPDIR:-tmp_mem_addr_eq_guard}"
mkdir -p "$WD/w1" "$WD/w2"

# `x == K` guarding `x - c` narrows the else arm, so the address proves in range.
if ! "$LHD" compile "$GOOD" --workdir "$WD/w1" -q >"$WD/good.log" 2>&1; then
  echo "FAIL: guarded previous-slot address was rejected"
  cat "$WD/good.log"
  exit 1
fi

# The refinement must stay sound: a guard on another value leaves -1 reachable.
if "$LHD" compile "$BAD" --workdir "$WD/w2" -q >"$WD/bad.log" 2>&1; then
  echo "FAIL: unsound refinement, the cur == 3 guard accepted a -1 address"
  exit 1
fi
if ! grep -q "may fall outside" "$WD/bad.log"; then
  echo "FAIL: bad twin failed for the wrong reason"
  cat "$WD/bad.log"
  exit 1
fi
echo PASS
