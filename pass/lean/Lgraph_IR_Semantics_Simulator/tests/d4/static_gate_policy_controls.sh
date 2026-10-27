#!/usr/bin/env bash
# ONE static-gate policy for verified_compiler output, and the shape rule that
# policy actually needs.
#
# WHAT WENT WRONG.  `pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh` kept its own inline copy
# of the verified_compiler gates, and that copy drifted: it demanded
# `nodes > 0`. A design whose outputs are ALL constant-driven has zero COMPUTED
# nodes and is perfectly valid -- `null_vpu` (10 sources, 0 nodes) and
# `minion_dcache_texsend` (7 sources, 0 nodes) are exactly that -- so both were
# rejected at emission while passing every canonical gate and the certificate
# invariant. What IS invalid is an EMPTY certificate: no sources AND no nodes.
#
# The emission path now CALLS `pass/lean/Exporter/DesignCert/scripts/vc_gates.py` instead of
# restating the rules. These controls pin BOTH halves of that: the path really
# delegates, and the policy it delegates to is not weaker than the copy it
# replaced.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
GATE="$ROOT/pass/lean/Exporter/DesignCert/scripts/vc_gates.py"
EMIT="$ROOT/pass/lean/Lgraph_IR_Semantics_Simulator/scripts/run_coreet_module_lean.sh"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
for f in "$GATE" "$EMIT"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done

mkdir -p "$ROOT/temp"
W="$(mktemp -d "$ROOT/temp/sgpol.XXXXXX")"
trap 'rm -rf "$W"' EXIT

# ---- 1. the emission path DELEGATES ---------------------------------------
vc_branch="$(awk '/^if \[\[ "\$LEAN_MODE" == "verified_compiler" \]\]; then$/{f=1} f{print} /^else$/{if(f) exit}' "$EMIT")"
grep -q 'vc_gates.py' <<<"$vc_branch" \
  && ok "emission path: the verified_compiler branch calls vc_gates.py" \
  || bad "emission path: the verified_compiler branch does not call vc_gates.py"
# No EXECUTABLE node-count rule may remain (a comment explaining the history is
# fine; a `[[ ... -gt 0 ]]` on the node count is not).
if grep -vE '^\s*#' <<<"$vc_branch" | grep -qE '\$n_nod|origin :=.*grep|-gt 0'; then
  bad "emission path: an inline node-count rule is still executable in that branch"
else
  ok "emission path: no divergent inline node-count rule remains"
fi
grep -q 'gate_status=1' <<<"$vc_branch" \
  && ok "emission path: a failing gate still sets gate_status (fail-closed preserved)" \
  || bad "emission path: a failing gate no longer propagates"

# ---- a minimal, well-formed SOURCE-ONLY generated file --------------------
mk() { # mk <file> <sources-block> [extra-decls] [opts]
  local f="$1" srcs="$2" extra="${3-}" opts="${4-both}"
  : > "$f"
  case "$opts" in
    both)    echo 'set_option maxRecDepth 1000000' >> "$f"; echo 'set_option maxHeartbeats 0' >> "$f" ;;
    norec)   echo 'set_option maxHeartbeats 0' >> "$f" ;;
    noheart) echo 'set_option maxRecDepth 1000000' >> "$f" ;;
    none)    ;;
    *)       echo "FATAL: unknown fixture opts $opts" >&2; exit 2 ;;
  esac
  cat >> "$f" <<EOF
def probe_designCert : DesignCert :=
  { sources := #[
$srcs
    ]
    nodes := #[
    ]
    outputs := #[
      { slot := 0, width := 8 }
    ]
    flops := #[
    ]
    memories := #[
    ]
  }

def probe_step : RuntimeInput → RuntimeState → RuntimeResult :=
  compileAndRun probe_designCert

theorem probe_compiles : compilesOk probe_designCert = true := by
  native_decide

theorem probe_step_correct : ∀ inp st,
    probe_step inp st = interpretDesign probe_designCert inp st :=
  compileAndRun_correct probe_designCert probe_compiles
$extra
EOF
}
SRC_OK='      SourceDesc.const 8 ((Int.ofNat 7))
    , SourceDesc.input 0 8'

run_gate() { python3 -B "$GATE" "$1" probe 2>&1; }
expect_pass() { local out; out="$(run_gate "$2")"; if [[ $? -ne 0 ]]; then bad "$1: REJECTED -- $(grep -m1 '^FAIL' <<<"$out")"; else ok "$1"; fi; }
expect_fail() { # expect_fail <label> <file> <needle>
  local out; out="$(run_gate "$2")"
  if [[ $? -eq 0 ]]; then bad "$1: ACCEPTED"
  elif ! grep -q "$3" <<<"$out"; then bad "$1: failed for another reason -- $(grep -m1 '^FAIL' <<<"$out")"
  else ok "$1"; fi
}

# ---- 2. the shape rule, both directions -----------------------------------
mk "$W/srconly.lean" "$SRC_OK"
expect_pass "a well-formed SOURCE-ONLY design (sources>0, nodes=0) PASSES" "$W/srconly.lean"

mk "$W/empty.lean" ""
expect_fail "a truly EMPTY certificate (no sources, no nodes) FAILS" "$W/empty.lean" \
            "empty certificate"

# ---- 3. nothing the inline copy caught may be lost ------------------------
for d in _designCert _step _compiles _step_correct; do
  mk "$W/miss$d.lean" "$SRC_OK"
  # Rename the PREFIX, not with a suffix: `probe_compiles_RENAMED` still
  # CONTAINS `probe_compiles`, so a suffix leaves the gate's substring test
  # satisfied and the fixture tests nothing.
  sed -i "s/probe$d/zzz$d/g" "$W/miss$d.lean"
  expect_fail "a missing probe$d declaration FAILS" "$W/miss$d.lean" "missing probe$d"
done

mk "$W/norec.lean" "$SRC_OK" "" norec
expect_fail "a missing set_option maxRecDepth FAILS" "$W/norec.lean" "maxRecDepth"
mk "$W/noheart.lean" "$SRC_OK" "" noheart
expect_fail "a missing set_option maxHeartbeats FAILS" "$W/noheart.lean" "maxHeartbeats"

mk "$W/resid.lean" "$SRC_OK" 'theorem probe_bad : probe_residual = probe_residual := rfl'
expect_fail "a THEOREM statement naming a ResidualProgram FAILS" "$W/resid.lean" "ResidualProgram"

mk "$W/resid2.lean" "$SRC_OK" 'theorem probe_bad2 :
    probe_residual = probe_residual :=
  rfl'
expect_fail "a MULTI-LINE theorem naming a ResidualProgram FAILS" "$W/resid2.lean" "ResidualProgram"

mk "$W/sorry.lean" "$SRC_OK" 'theorem probe_todo : True := by sorry'
expect_fail "a \`sorry\` FAILS" "$W/sorry.lean" "sorry"

# ...and the documented carve-out survives: a `def <Top>_residual` is emitted
# deliberately for #eval and must NOT trip the theorem rule.
mk "$W/defresid.lean" "$SRC_OK" 'def probe_residual : ResidualProgram :=
  match compileDesign probe_designCert with
  | _ => default'
expect_pass "a \`def probe_residual\` (not a theorem) still PASSES" "$W/defresid.lean"

# ---- 4. the two checks UNIFYING the path GAINS ----------------------------
mk "$W/cwidth.lean" '      SourceDesc.const 2 ((Int.ofNat 8))'
expect_fail "a constant too wide for its declared width FAILS (gained by unifying)" \
            "$W/cwidth.lean" "cannot hold value 8"
mk "$W/rom.lean" '      SourceDesc.memConst 2 3 #[1, 9, 2, 0]'
expect_fail "a ROM entry too wide for its table dw FAILS (gained by unifying)" \
            "$W/rom.lean" "does not fit dw=3"

# ---- 5. the REAL retained source-only artifacts ---------------------------
RUN="${D4_R2_RUN:-$ROOT/temp/coreet_122_run_49cf}"
for m in null_vpu minion_dcache_texsend; do
  f="$RUN/blocks/$m/lean/${m}_Lgraph.lean"
  if [[ ! -s "$f" ]]; then
    printf 'N/A   no retained %s artifact; that leg was SKIPPED\n' "$m"
    continue
  fi
  n_src="$(grep -c 'SourceDesc\.' "$f")"; n_nod="$(grep -c 'origin :=' "$f")"
  if [[ "$n_nod" -ne 0 || "$n_src" -le 0 ]]; then
    bad "$m: retained artifact is not the source-only shape (sources=$n_src nodes=$n_nod)"
  elif ! python3 -B "$GATE" "$f" "$m" --quiet >/dev/null 2>&1; then
    bad "$m: the real source-only artifact is rejected by the canonical gate"
  else
    ok "$m: the real source-only artifact (sources=$n_src nodes=0) passes the canonical gate"
  fi
done

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
