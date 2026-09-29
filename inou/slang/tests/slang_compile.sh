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
#   roundtrip - the lec tier on the Verilog re-emitted from the fixture's
#             emitted Pyrope (the Pyrope writer round trip).
#   roundtrip_sim - the Pyrope round trip, value-checked instead of LEC'd (for
#             constructs the formal encoder refuses). Every
#             `// :verilog_re: <ERE>` header must match the re-emitted Verilog
#             and no `// :verilog_not_re: <ERE>` may. Then the sibling
#             `<stem>_tb.prp` runs under `lhd sim lg:` (native, cycle-based
#             directed vectors: it cannot see edge polarity or reset timing
#             between edges, which the regex headers pin structurally). A design
#             native sim refuses declares `// :sim_unsupported: <substr>`
#             instead; the refusal (exit 7, class unsupported, <substr> in the
#             message) is then required, so a newly supported schedule fails
#             until it gets a _tb.prp. An optional `<stem>_tb.v` event-level
#             bench runs under iverilog/vvp only when LHD_EXTERNAL_SIM is set.
# Per-fixture headers (auto mode): `// :test: <tier>`, `// :top: <module>`,
# `// :lec_timeout: <seconds>` and `// :lec_solver: <name>` (lec tiers:
# runs `lhd lec --set formal.solver=<name>`; lgyosys cross-checks with lgcheck
# under LGCHECK_EQUIV_TIMEOUT=<lec_timeout> and requires its unbounded proof,
# crosscheck exit_code 0).
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

header_values() { # <file> <key>: every `// :<key>: <value>` value, one per line
  sed -nE "s@^//[[:space:]]*:$2:[[:space:]]*(.*[^[:space:]])[[:space:]]*\$@\\1@p" "$1"
}

# The roundtrip_sim checks on the re-emitted "$wd"/all.v (see the tier list).
run_roundtrip_sim() { # <file> <base> <scratch>
  local f=$1 base=$2 wd=$3 re unsupported tb tool rc
  while IFS= read -r re; do
    grep -Eq -- "$re" "$wd"/all.v || {
      echo "FAIL(${base}): emitted Verilog does not match :verilog_re: $re"
      return 1
    }
  done < <(header_values "$f" verilog_re)
  while IFS= read -r re; do
    if grep -Eq -- "$re" "$wd"/all.v; then
      echo "FAIL(${base}): emitted Verilog matches :verilog_not_re: $re"
      grep -En -- "$re" "$wd"/all.v
      return 1
    fi
  done < <(header_values "$f" verilog_not_re)

  unsupported=$(header_values "$f" sim_unsupported | head -1)
  if [ -n "$unsupported" ]; then
    # A tripwire, not a behavior check: native sim must keep refusing loudly
    # (never simulate a clock it cannot schedule as if it ticked every step).
    rc=0
    ${LHD} compile "$wd"/all.v --top "$base" --set compile.formal.mode=none \
      --emit-dir sim:"$wd"/sim --workdir "$wd"/simw --result-json "$wd"/sim.json \
      -q >"$wd"/sim.log 2>&1 || rc=$?
    python3 - "$wd"/sim.json "$rc" "$unsupported" <<'CHECK' || { cat "$wd"/sim.log; return 1; }
import json, sys
path, rc, want = sys.argv[1], int(sys.argv[2]), sys.argv[3]
if rc == 0:
    raise SystemExit('FAIL: native simulation now supports this design: replace '
                     ':sim_unsupported: with a <stem>_tb.prp testbench')
with open(path) as stream:
    error = json.load(stream).get('error', {})
if rc != 7 or error.get('class') != 'unsupported' or want not in error.get('message', ''):
    raise SystemExit('FAIL: expected a native-sim refusal (exit 7, class unsupported) '
                     'mentioning {!r}; got exit {}: {}'.format(want, rc, error))
CHECK
  else
    tb=${f%.*}_tb.prp
    [ -s "$tb" ] || {
      echo "FAIL(${base}): roundtrip_sim needs a Pyrope testbench $tb (or a :sim_unsupported: header)"
      return 1
    }
    ${LHD} compile "$wd"/all.v --top "$base" --set compile.formal.mode=none \
      --emit-dir lg:"$wd"/lg --workdir "$wd"/lgw -q >"$wd"/lg.log 2>&1 || {
      echo "FAIL(${base}): re-emitted Verilog did not compile to lg:"
      cat "$wd"/lg.log
      return 1
    }
    ${LHD} sim lg:"$wd"/lg "$tb" --set sim.ninja=false --set sim.tune.profile=off \
      --set compile.upass.inline=false --set sim.unknown_zero=true \
      --workdir "$wd"/simrun --result-json "$wd"/simrun.json -q >"$wd"/simrun.log 2>&1 || {
      echo "FAIL(${base}): native simulation of $tb failed"
      cat "$wd"/simrun.log "$wd"/simrun.json 2>/dev/null
      return 1
    }
    # A bench whose test block never ran must not pass vacuously.
    python3 - "$wd"/simrun.json <<'CHECK' || return 1
import json, sys
with open(sys.argv[1]) as stream:
    tests = json.load(stream).get('tests', [])
if not tests or any(t.get('status') != 'pass' for t in tests):
    raise SystemExit('FAIL: expected at least one passing native test, got {}'.format(tests))
CHECK
  fi

  # Event-level oracle (edges between reference cycles), external tools only.
  tb=${f%.*}_tb.v
  if [ -s "$tb" ]; then
    if [ -n "${LHD_EXTERNAL_SIM:-}" ]; then
      for tool in iverilog vvp; do
        command -v "$tool" >/dev/null 2>&1 || {
          echo "FAIL(${base}): LHD_EXTERNAL_SIM is set but $tool is not on PATH"
          return 1
        }
      done
      iverilog -g2012 -s tb -o "$wd"/event_sim "$wd"/all.v "$tb" && vvp -n "$wd"/event_sim || {
        echo "FAIL(${base}): event-level bench $tb failed"
        return 1
      }
    else
      echo "note: external-simulator leg skipped (set LHD_EXTERNAL_SIM=1)"
    fi
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
  # Optional solver override (`lgyosys` = native engine + lgcheck cross-check).
  # lgcheck's own equivalence budget is otherwise far above the watchdog, so a
  # regression would surface only as a watchdog kill instead of a DISAGREE.
  local lec_solver
  lec_solver=$(sed -nE 's@^//[[:space:]]*:lec_solver:[[:space:]]*([a-z0-9_]+).*@\1@p' "$f" | head -1)
  wd=${TEST_TMPDIR:-.}/tmp_slang/${name}
  rm -rf "$wd"
  mkdir -p "$wd"
  local solver_args=()
  [ -z "$lec_solver" ] || solver_args=(--set formal.solver="$lec_solver" --result-json "$wd"/lec.json)

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
        run_roundtrip_sim "$f" "$base" "$wd" || return 1
      else
      LGCHECK_EQUIV_TIMEOUT="${lec_timeout}" python3 inou/prp/tests/lec.py --sanity --timeout "${lec_timeout}" -- \
        "${LHD}" lec --impl verilog:"$wd"/all.v --ref verilog:"$f" --top "$base" \
        ${solver_args[@]+"${solver_args[@]}"} \
        --workdir "$wd"/wc -q >"$wd"/check.log 2>&1 || {
        echo "FAIL(${base}): LEC check failed"
        cat "$wd"/check.log
        return 1
      }
      # Cross mode also accepts a bounded-clean lgcheck; an opted-in fixture
      # requires lgcheck's unbounded proof (exit 0).
      if [ "$lec_solver" = lgyosys ]; then
        python3 - "$wd"/lec.json <<'CHECK' || { cat "$wd"/check.log; return 1; }
import json, sys
with open(sys.argv[1]) as stream:
    cross = json.load(stream).get('lec', {}).get('crosscheck', {})
if cross.get('solver') != 'lgyosys' or cross.get('exit_code') != 0:
    raise SystemExit('FAIL: the lgyosys cross-check is not an unbounded lgcheck proof: {}'.format(cross))
CHECK
      fi
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
