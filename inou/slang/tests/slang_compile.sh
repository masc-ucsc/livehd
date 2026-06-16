#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Direct slang front-end (SV -> LNAST) test driver (todo/ 2s subtask E).
#
# Ladder mode (the bazel slang_compile-* targets):
#   slang_compile.sh <tier> <file.v>
# with tier one of:
#   lec     - compile --reader slang to verilog AND lhd lec --set lec.solver=lgyosys (LEC) it against
#             the source itself. The strongest tier.
#   verilog - compile to verilog must succeed (a known LEC gap is tracked in
#             slang_ladder.bzl next to the entry).
#   lnast   - compile to ln:/lnast-dump + `lhd compile ln:` reload round-trip
#             (the serialization tier; the construct does not reach tolg yet).
#   error   - the compile MUST fail cleanly: non-zero exit and at least one
#             structured diagnostic (no crash/abort).
# Per-tier expectations are an acceptance gate both ways: a regression fails
# its tier, and an `error` entry that starts compiling also FAILS so the
# ladder gets promoted explicitly in slang_ladder.bzl.
#
# Legacy mode (no tier argument): every inou/slang/tests/verilog/*.v at the
# `error` tier (the sky130 cell-instance set pins the unknown-module policy).

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x $LHD ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAILED: slang_compile.sh could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

run_lnast_tier() { # <file> <base> <scratch>
  local f=$1 base=$2 wd=$3
  ${LHD} compile "$f" --reader slang \
    --emit-dir ln:"$wd"/ln/ --emit-dir lnast-dump:"$wd"/dump/ \
    --workdir "$wd"/w -q >"$wd"/compile.log 2>&1 || {
    echo "FAIL(${base}): slang LNAST parsing/upass failed"
    cat "$wd"/compile.log
    return 1
  }
  shopt -s nullglob
  local dumps=("$wd"/dump/*.lnast)
  shopt -u nullglob
  if [ ${#dumps[@]} -eq 0 ] || [ ! -s "${dumps[0]}" ]; then
    echo "FAIL(${base}): LNAST dump empty or missing"
    return 1
  fi
  ${LHD} compile ln:"$wd"/ln/ --workdir "$wd"/wreload -q >"$wd"/reload.log 2>&1 || {
    echo "FAIL(${base}): ln: reload/upass failed"
    cat "$wd"/reload.log
    return 1
  }
  return 0
}

run_verilog_tier() { # <file> <base> <scratch>
  local f=$1 base=$2 wd=$3
  ${LHD} compile "$f" --reader slang --top "$base" \
    --emit-dir verilog:"$wd"/v/ --workdir "$wd"/w -q >"$wd"/compile.log 2>&1 || {
    echo "FAIL(${base}): slang -> verilog compile failed"
    cat "$wd"/compile.log
    return 1
  }
  cat "$wd"/v/*.v >"$wd"/all.v 2>/dev/null
  if [ ! -s "$wd"/all.v ]; then
    echo "FAIL(${base}): no verilog emitted"
    return 1
  fi
  return 0
}

run_one() { # <tier> <file>
  local tier=$1 f=$2
  local name base wd
  name=$(basename "$f" .v)
  base=${name#long_}
  base=${base#fixme_}
  base=${base#nocheck_}
  base=${base#long_}
  wd=tmp_slang/${name}
  rm -rf "$wd"
  mkdir -p "$wd"

  case "$tier" in
    lnast)
      run_lnast_tier "$f" "$base" "$wd" || return 1
      ;;
    verilog)
      run_verilog_tier "$f" "$base" "$wd" || return 1
      ;;
    lec)
      run_verilog_tier "$f" "$base" "$wd" || return 1
      ${LHD} lec --set lec.solver=lgyosys --impl verilog:"$wd"/all.v --ref verilog:"$f" --top "$base" \
        --workdir "$wd"/wc -q >"$wd"/check.log 2>&1 || {
        echo "FAIL(${base}): LEC mismatch vs source"
        tail -5 "$wd"/check.log
        return 1
      }
      ;;
    error)
      if ${LHD} compile "$f" --reader slang --emit-dir lnast-dump:"$wd"/dump/ \
        --emit diagnostics:"$wd"/diag.jsonl --workdir "$wd"/w -q >"$wd"/compile.log 2>&1; then
        echo "FAIL(${base}): expected a compile error but the compile passed — promote the ladder entry"
        return 1
      fi
      if [ ! -s "$wd"/diag.jsonl ]; then
        echo "FAIL(${base}): compile failed without a structured diagnostic (crash?)"
        cat "$wd"/compile.log
        return 1
      fi
      ;;
    *)
      echo "FAIL: unknown tier '$tier'"
      return 1
      ;;
  esac
  echo "PASS(${base}) tier=${tier}"
  return 0
}

if [ $# -ge 2 ]; then
  run_one "$1" "$2"
  exit $?
fi

# Legacy default mode: the sky130 cell-instance set. These instantiate
# liberty cells with no module sources, so they pin the unknown-module
# (blackbox) diagnostic policy: a clean located error, never a crash.
fail=0
for f in inou/slang/tests/verilog/*.v; do
  run_one error "$f" || fail=1
done
exit $fail
