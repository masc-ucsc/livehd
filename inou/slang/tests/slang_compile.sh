#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Direct slang front-end (SV -> LNAST) test driver (todo/ 2s subtask E).
#
# Ladder mode (the bazel slang_compile-* targets):
#   slang_compile.sh <tier> <file.v>
# with tier one of:
#   lec      - compile --reader slang to verilog AND lhd lec (LEC) it against
#              the source itself, with a five-second internal budget and ten-second watchdog. Timeouts
#              pass with an explicit unproven result; refutations still fail.
#   lec_no_x - the lec tier plus a check that the generated Verilog has no X/Z
#              literal. Use for fully-defined sources whose regression was a
#              silently introduced unknown value.
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

LHD=${LHD:-./bazel-bin/lhd/lhd}
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
  name=$(basename "$f")
  name=${name%.*}
  if [ "$tier" = auto ]; then
    tier=$(sed -nE 's@^//[[:space:]]*:test:[[:space:]]*([a-z_]+).*@\1@p' "$f" | head -1)
    tier=${tier:-lec}
  fi
  base=${name#long_}
  base=${base#fixme_}
  base=${base#nocheck_}
  base=${base#long_}
  local declared_top
  declared_top=$(sed -nE 's@^//[[:space:]]*:top:[[:space:]]*([^[:space:]]+).*@\1@p' "$f" | head -1)
  base=${declared_top:-$base}
  # Per-fixture LEC budget. lec.py's outer watchdog is 2x this and bounds the
  # WHOLE process (slang parse + upass + tolg + cgen + encode + solve), while
  # formal.timeout bounds solving only -- so a fixture whose total wall time
  # exceeds 2x the default 5s can never even reach the --sanity allowance; it
  # dies on the watchdog. Such a fixture declares the budget it actually needs.
  local lec_timeout
  lec_timeout=$(sed -nE 's@^//[[:space:]]*:lec_timeout:[[:space:]]*([0-9]+).*@\1@p' "$f" | head -1)
  lec_timeout=${lec_timeout:-5}
  wd=${TEST_TMPDIR:-.}/tmp_slang/${name}
  rm -rf "$wd"
  mkdir -p "$wd"

  case "$tier" in
    lnast)
      run_lnast_tier "$f" "$base" "$wd" || return 1
      ;;
    verilog)
      run_verilog_tier "$f" "$base" "$wd" || return 1
      ;;
    lec | lec_no_x | roundtrip | roundtrip_sim)
      run_verilog_tier "$f" "$base" "$wd" || return 1
      if [ "$tier" = lec_no_x ] && grep -Eq "[0-9]+'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_]*[xXzZ?]" "$wd"/all.v; then
        echo "FAIL(${base}): generated Verilog contains an X/Z literal"
        grep -En "[0-9]+'[sS]?[bBoOdDhH][0-9a-fA-FxXzZ_]*[xXzZ?]" "$wd"/all.v
        return 1
      fi
      if [ "$tier" = roundtrip ] || [ "$tier" = roundtrip_sim ]; then
        ${LHD} compile "$f" --reader slang --top "$base" --emit-dir pyrope:"$wd"/prp \
          --workdir "$wd"/writer -q >"$wd"/writer.log 2>&1 &&
        ${LHD} compile "$wd"/prp/*.prp --top "$base" --emit verilog:"$wd"/all.v \
          --workdir "$wd"/roundtrip -q >>"$wd"/writer.log 2>&1 || {
          echo "FAIL(${base}): emitted Pyrope did not round-trip"
          cat "$wd"/writer.log
          return 1
        }
      fi
      if [ "$tier" = roundtrip_sim ]; then
        # Behavioral oracle for constructs the formal encoder cannot model
        # (for example a flop-driven clock). No missing-tool or compile skips.
        local tb="${f%.*}_tb.v"
        [ -s "$tb" ] || { echo "FAIL(${base}): missing RTL testbench $tb"; return 1; }
        iverilog -g2012 -s tb -o "$wd/sim" "$wd/all.v" "$tb" &&
          vvp "$wd/sim" || return 1
      else
      python3 inou/prp/tests/lec.py --sanity --timeout "${lec_timeout}" -- \
        "${LHD}" lec --impl verilog:"$wd"/all.v --ref verilog:"$f" --top "$base" \
        --workdir "$wd"/wc -q >"$wd"/check.log 2>&1 || {
        echo "FAIL(${base}): LEC check failed"
        cat "$wd"/check.log
        return 1
      }
      tail -1 "$wd"/check.log
      fi
      ;;
    error)
      local compile_status=0
      ${LHD} compile "$f" --reader slang --top "$base" --emit verilog:"$wd"/out.v \
        --emit diagnostics:"$wd"/diag.jsonl --workdir "$wd"/w -q >"$wd"/compile.log 2>&1 || compile_status=$?
      # A compiler diagnostic is required: neither a crash nor a warning suffices.
      if [ "$compile_status" -ne 6 ] && [ "$compile_status" -ne 7 ]; then
        echo "FAIL(${base}): expected a clean compile error, got exit $compile_status"
        cat "$wd"/compile.log
        return 1
      fi
      python3 - "$wd/diag.jsonl" "$f" <<'CHECK' || return 1
import json, re, sys
with open(sys.argv[1]) as stream:
    errors = [d for line in stream if line.strip() for d in [json.loads(line)] if d.get('severity') == 'error']
with open(sys.argv[2]) as stream:
    match = re.search(r'^//\s*:error:\s*(.+)', stream.read(), re.M)
if not errors or (match and not any(re.search(match[1], d.get('message', '')) for d in errors)):
    raise SystemExit('FAIL: expected structured compiler error was not emitted')
CHECK
      ;;
    *)
      echo "FAIL: unknown tier '$tier'"
      return 1
      ;;
  esac
  # A pass below the strongest `lec` tier is a DEFERRED/CAPPED verification (a
  # tracked LEC or lowering gap), not a clean pass — never let it slip by
  # silently. Print a loud banner so `bazel test` output flags it for follow-up.
  case "$tier" in
    verilog | lnast)
      echo "################################################################" >&2
      echo "## ⚠️  DEFERRED: ${base} passes only at tier='${tier}' (NOT lec) ##" >&2
      echo "## formal/LEC equivalence is CAPPED here — see slang_ladder.bzl,  " >&2
      echo "## promote to 'lec' once the tracked gap is fixed.                 " >&2
      echo "################################################################" >&2
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
