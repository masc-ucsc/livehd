#!/usr/bin/env bash
# Controls for the constant-folded-GetMask exemption in cert_lgraph_diff.py.
#
# THE EXEMPTION'S CONTRACT: a wide certificate constant absent from the cgen
# Verilog is excused only when EVERY consumer of EVERY source slot carrying that
# value is a constant-folded `Op_GetMask`, AND the folded results those nodes
# produce are themselves present in the Verilog.
#
# THE DEFECT THESE PIN.  Eligibility was aggregated by numeric VALUE with
# `setdefault(v, None)`, which cannot overwrite an existing entry.  A qualifying
# slot visited FIRST stored a result set that a later disqualifying slot could
# not remove, so a value with one folded use and one ORDINARY use was exempted
# anyway -- order-dependent and fail-open.  Direct uses by an output, flop or
# memory descriptor were not considered at all.
#
# Every fixture is generated here, so the controls do not depend on any corpus.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../../.." && pwd)"
GATE="$ROOT/pass/lean/Exporter/Frontend/scripts/cert_lgraph_diff.py"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -r "$GATE" ]] || { echo "FATAL: no $GATE" >&2; exit 2; }

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/diffex.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

V=562949953421280          # 49 bits; the value the real corpus flagged
MASK=3                     # low two bits -> folded result 0 for src V

# mk_cert <file> <sources-body> <nodes-body> <outputs-body>
mk_cert() {
  cat > "$1" <<EOF
-- synthetic fixture for differential_exemption_controls.sh
def probe_designCert : DesignCert :=
  { sources := #[
$2
    ]
    nodes := #[
$3
    ]
    outputs := #[
$4
    ]
    flops := #[]
    memories := #[]
  }
EOF
}
# A Verilog fixture holding only the folded results, never the wide operand.
mk_v() { printf 'module probe; wire x = 64%s; wire y = 64%s; endmodule\n' "'d0" "'d1" > "$1"; }

run_gate() { python3 -B "$GATE" "$1" --verilog "$2" --require-verilog 2>&1; }

SRC_V="      SourceDesc.const 64 ((Int.ofNat $V))"
SRC_M="    , SourceDesc.const 64 ((Int.ofNat $MASK))"
SRC_IN="    , SourceDesc.input 0 64"
GM="      { op := LGraphOp.Op_GetMask, width := 2, deps := #[0, 1], origin := 1 }"

mk_v "$TMP/probe.v"

# --- 1. POSITIVE: one slot, folded-only, folded result present -> EXEMPT ---
mk_cert "$TMP/pos.lean" "$SRC_V
$SRC_M" "$GM" "      { slot := 4, width := 2 }"
out="$(run_gate "$TMP/pos.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -ne 0 ]]; then
  bad "positive control rejected a legitimately folded constant: $(grep -m1 '^FAIL' <<<"$out")"
elif ! grep -q "^EXEMPT: $V " <<<"$out"; then
  bad "positive control did not report the exemption"
else
  ok "folded-only constant with its folded result in the Verilog: EXEMPT, gate passes"
fi

# --- 2. QUALIFYING FIRST, then an ordinary consumer -----------------------
# The reviewer's reproducer: slot 0 feeds a folded GetMask, slot 2 holds the
# SAME value and feeds Op_And.
mk_cert "$TMP/qf.lean" "$SRC_V
$SRC_M
    , SourceDesc.const 64 ((Int.ofNat $V))
$SRC_IN" "$GM
      , { op := LGraphOp.Op_And, width := 64, deps := #[2, 3], origin := 2 }" \
"      { slot := 4, width := 2 }
      , { slot := 5, width := 64 }"
out="$(run_gate "$TMP/qf.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "QUALIFYING-FIRST: a value with an ordinary consumer was still exempted"
elif grep -q "^EXEMPT: $V " <<<"$out"; then
  bad "QUALIFYING-FIRST: the value was reported EXEMPT despite an ordinary consumer"
elif ! grep -q "cert holds constant $V" <<<"$out"; then
  bad "QUALIFYING-FIRST failed for the wrong reason: $(grep -m1 '^FAIL' <<<"$out")"
else
  ok "qualifying slot first, ordinary consumer second: value disqualified, gate fails"
fi

# --- 3. DISQUALIFYING FIRST, then a qualifying occurrence -----------------
mk_cert "$TMP/df.lean" "      SourceDesc.const 64 ((Int.ofNat $V))
$SRC_IN
    , SourceDesc.const 64 ((Int.ofNat $V))
    , SourceDesc.const 64 ((Int.ofNat $MASK))" \
"      { op := LGraphOp.Op_And, width := 64, deps := #[0, 1], origin := 1 }
      , { op := LGraphOp.Op_GetMask, width := 2, deps := #[2, 3], origin := 2 }" \
"      { slot := 4, width := 64 }
      , { slot := 5, width := 2 }"
out="$(run_gate "$TMP/df.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "DISQUALIFYING-FIRST: a later qualifying occurrence revived the value"
elif grep -q "^EXEMPT: $V " <<<"$out"; then
  bad "DISQUALIFYING-FIRST: the value was reported EXEMPT"
else
  ok "disqualifying slot first, qualifying second: stays disqualified, gate fails"
fi

# --- 4. ORDINARY CONSUMER ONLY --------------------------------------------
mk_cert "$TMP/ord.lean" "      SourceDesc.const 64 ((Int.ofNat $V))
$SRC_IN" \
"      { op := LGraphOp.Op_And, width := 64, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 64 }"
out="$(run_gate "$TMP/ord.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then bad "ORDINARY CONSUMER: the gate passed"
elif grep -q "^EXEMPT: $V " <<<"$out"; then bad "ORDINARY CONSUMER: reported EXEMPT"
else ok "ordinary node consumer only: never exempt, gate fails"; fi

# --- 5. DIRECT DESCRIPTOR USE (an output on the source slot) --------------
# Slot 0 holds V, feeds a folded GetMask, AND is read directly by an output.
mk_cert "$TMP/dir.lean" "$SRC_V
$SRC_M" "$GM" "      { slot := 0, width := 64 }
      , { slot := 4, width := 2 }"
out="$(run_gate "$TMP/dir.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "DIRECT OUTPUT: a source read straight out of the design was still exempted"
elif grep -q "^EXEMPT: $V " <<<"$out"; then
  bad "DIRECT OUTPUT: reported EXEMPT"
else
  ok "source also read directly by an output: never exempt, gate fails"
fi

# --- 6. FOLDED RESULT ABSENT FROM THE VERILOG -----------------------------
# src = mask = V, so the folded result is 2^44-1, which the fixture omits.
mk_cert "$TMP/fr.lean" "$SRC_V
    , SourceDesc.const 64 ((Int.ofNat $V))" \
"      { op := LGraphOp.Op_GetMask, width := 44, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 44 }"
out="$(run_gate "$TMP/fr.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "FOLDED RESULT ABSENT: the gate passed anyway"
elif ! grep -q 'folded results' <<<"$out"; then
  bad "FOLDED RESULT ABSENT failed for the wrong reason: $(grep -m1 '^FAIL' <<<"$out")"
else
  ok "folded result missing from the Verilog: $(grep -m1 'folded results' <<<"$out" | cut -c1-70)..."
fi

# --- 7. --require-verilog still fails closed ------------------------------
out="$(python3 -B "$GATE" "$TMP/pos.lean" --require-verilog --quiet 2>&1)"; rc=$?
if [[ "$rc" -eq 0 ]]; then bad "--require-verilog passed with no Verilog supplied"
elif ! grep -q 'differential half did NOT run' <<<"$out"; then
  bad "--require-verilog failed for the wrong reason: $out"
else ok "--require-verilog with no artifact: fails closed"; fi

# --- 8. the positive-constant fit invariant is untouched ------------------
mk_cert "$TMP/fit.lean" "      SourceDesc.const 2 ((Int.ofNat 8))
$SRC_M" "      { op := LGraphOp.Op_GetMask, width := 2, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 2 }"
out="$(run_gate "$TMP/fit.lean" "$TMP/probe.v")"; rc=$?
if [[ "$rc" -eq 0 ]]; then bad "the fit invariant no longer rejects const width 2 holding 8"
elif ! grep -q 'const width 2 cannot hold value 8' <<<"$out"; then
  bad "fit invariant reported something else: $(grep -m1 '^FAIL' <<<"$out")"
else ok "positive-constant fit invariant still rejects width 2 holding 8"; fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
