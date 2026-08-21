#!/bin/bash

set -eu

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi

src=$1
wd=tmp_slang/deferred_for_runtime
rm -rf "$wd"
mkdir -p "$wd"

if "$LHD" compile "$src" --reader slang --top deferred_for_runtime \
  --emit verilog:"$wd"/out.v --emit diagnostics:"$wd"/diag.jsonl \
  --workdir "$wd"/w -q >"$wd"/compile.log 2>&1; then
  echo "FAIL: a genuinely runtime SystemVerilog for bound compiled as a comptime loop"
  exit 1
fi

if grep -q '"severity":"error".*"pass":"inou.slang"' "$wd"/diag.jsonl; then
  echo "FAIL: inou.slang rejected the deferred loop before uPass could resolve it"
  cat "$wd"/diag.jsonl
  exit 1
fi

grep -q '"pass":"upass.tolg".*non-comptime.*for' "$wd"/diag.jsonl || {
  echo "FAIL: the unresolved loop did not produce the expected downstream comptime error"
  cat "$wd"/diag.jsonl
  exit 1
}

echo "PASS: unresolved loop bounds defer past inou.slang and fail at the uPass/tolg comptime boundary"
