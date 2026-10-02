#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The documented way to diagnose a surviving combinational cycle must produce
# something when run the documented way.
#
# graph/split_selfref.cpp's unresolved-cycle warning tells the reader to "run
# with LIVEHD_SIM_SPLIT_DEBUG=1 to see which", and the trace went to STDOUT --
# which `lhd` owns for its structured result. So every run through the driver
# printed nothing at all, and only the standalone gtest ever showed the trace.
# Measured on intpipe_csr_file: 2,155 lines of trace, none of them visible.
#
# The assertion is therefore about the STREAM, which is what was wrong -- not
# about any particular message.
set -u
LHD="${LHD:-lhd/lhd}"
[ -x "$LHD" ] || LHD=./bazel-bin/lhd/lhd
[ -x "$LHD" ] || { echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; }
LHD="$(cd "$(dirname "$LHD")" && pwd)/$(basename "$LHD")"
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
RTL="$HERE/packed_selfref_cycle.v"
[ -r "$RTL" ] || { echo "FAIL: missing fixture $RTL"; exit 1; }

if [ -n "${TEST_TMPDIR:-}" ]; then T="$TEST_TMPDIR/split_selfref_debug_runtime"
else T="$ROOT/generated/split_selfref_debug_test/runtime_tmp"; fi
rm -rf "$T"; mkdir -p "$T"
rc=0

run() {  # <tag> <env...>  -> $T/<tag>.{out,err}
  local tag="$1"; shift
  ( cd "$T" && env "$@" "$LHD" compile verilog "$RTL" --top packed_selfref_cycle \
      --reader yosys-slang --workdir "w_$tag" --emit-dir "lg:lg_$tag" ) \
      > "$T/$tag.out" 2> "$T/$tag.err"
  echo $?
}

# ---- the fixture still reaches the pass -----------------------------------
st="$(run on LIVEHD_SIM_SPLIT_DEBUG=1)"
if [ "$st" -ne 0 ]; then
  echo "FAIL: the fixture did not compile (rc=$st)"; tail -3 "$T/on.err" | sed 's/^/      /'; rc=1
fi
n_err="$(grep -c 'split\[dbg\]' "$T/on.err" || true)"
n_out="$(grep -c 'split\[dbg\]' "$T/on.out" || true)"

if [ "$n_err" -lt 1 ]; then
  echo "FAIL: LIVEHD_SIM_SPLIT_DEBUG=1 produced no split[dbg] trace on stderr."
  echo "      The unresolved-cycle warning tells the reader to set exactly this"
  echo "      variable, so a silent trace makes that advice useless."
  rc=1
else
  echo "ok: the trace reaches stderr ($n_err line(s))"
fi

# It must say what it actually looked at, not merely emit something.
if ! grep -q 'comb node(s),.*on a word-level cycle' "$T/on.err"; then
  echo "FAIL: the trace never reports how many nodes are on a word-level cycle,"
  echo "      which is the one number that says whether the pass saw the cycle"
  rc=1
else
  echo "ok: the trace reports the on-cycle node count: $(grep -oP '\d+ comb node\(s\), \d+ on a word-level cycle' "$T/on.err" | head -1)"
fi

# Each line the trace is USED for is pinned separately. A line sent to stdout
# does not show up there -- `lhd` swallows it -- it simply disappears, so
# "stdout is clean" cannot catch a single line going the wrong way.
if ! grep -q 'peel seeds=' "$T/on.err"; then
  echo "FAIL: the trace never reports the peel seeds, so the reader cannot tell"
  echo "      a graph with no acyclic starting point from one the pass refused"
  rc=1
else
  echo "ok: the trace reports the peel seeds: $(grep -oP 'peel seeds=\d+ of \d+' "$T/on.err" | head -1)"
fi

# stdout stays clean: it carries the structured result, and interleaving a
# debug trace into it is how a JSON consumer starts failing to parse.
if [ "$n_out" -ne 0 ]; then
  echo "FAIL: $n_out split[dbg] line(s) went to stdout, which carries the result JSON"
  rc=1
else
  echo "ok: stdout carries no trace (the result JSON is still parseable)"
fi

# ---- and it is OFF by default ---------------------------------------------
st="$(run off)"
if [ "$st" -ne 0 ]; then
  echo "FAIL: the fixture did not compile without the debug variable (rc=$st)"; rc=1
elif [ "$(grep -c 'split\[dbg\]' "$T/off.err" || true)" -ne 0 ]; then
  echo "FAIL: the trace appears without LIVEHD_SIM_SPLIT_DEBUG set"
  rc=1
else
  echo "ok: no trace unless the variable is set"
fi

[ "$rc" -eq 0 ] || { echo "FAIL: split_selfref_debug_test"; exit 1; }
echo "PASS: split_selfref_debug_test"
