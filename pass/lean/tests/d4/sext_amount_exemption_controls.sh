#!/usr/bin/env bash
# Controls for the structural Op_Sext AMOUNT exemption in cert_lgraph_diff.py.
#
# WHY THE EXEMPTION EXISTS.  `Op_Sext`'s operand 1 is not data: it encodes the
# SIGN POSITION, and `pass_lean.cpp` deliberately widens that constant to hold
# its value instead of truncating it to the pin width.  `inou.cgen.verilog`
# consumes a constant sign position STRUCTURALLY -- whole-operand replication, a
# bit-select, or a folded constant -- so the number need never appear as a sized
# Verilog literal.  The fresh CORE-ET baseline flagged exactly this on
# `vpu_tensorquant` (two 256s on 256-bit sign-extends of 256-bit flop sources).
#
# WHY IT IS NARROW.  It is NOT an exemption for 256, nor for absent constants.
# A value qualifies only when EVERY source slot carrying it is used solely as an
# arity-2 Sext amount at dependency index 1, is not read directly by an output,
# flop field or memory image, and EQUALS min(out_w, dep0_w).  A wrong amount is
# a real disagreement and stays a failure.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
GATE="$ROOT/pass/lean/scripts/cert_lgraph_diff.py"

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }
[[ -r "$GATE" ]] || { echo "FATAL: no $GATE" >&2; exit 2; }

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/sextex.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

A=300        # 9 bits, so the differential flags it; not special otherwise

mk_cert() {  # mk_cert <file> <sources> <nodes> <outputs> [flops] [memories]
  cat > "$1" <<EOF
-- synthetic fixture for sext_amount_exemption_controls.sh
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
    flops := #[
${5:-}
    ]
    memories := #[
${6:-}
    ]
  }
EOF
}
# Verilog holding unrelated literals only -- never $A.
printf 'module probe; wire a = 64%s; wire b = 64%s; endmodule\n' "'d7" "'d9" > "$TMP/probe.v"
run_gate() { python3 -B "$GATE" "$1" --verilog "$TMP/probe.v" --require-verilog 2>&1; }

expect_pass() {  # expect_pass <label> <cert>
  local out rc; out="$(run_gate "$2")"; rc=$?
  if [[ "$rc" -ne 0 ]]; then
    bad "$1: rejected -- $(grep -m1 '^FAIL' <<<"$out")"
  elif ! grep -q "^EXEMPT: $A .*Op_Sext amount" <<<"$out"; then
    bad "$1: passed without reporting the Sext-amount exemption"
  else ok "$1"; fi
}
expect_fail() {  # expect_fail <label> <cert>
  local out rc; out="$(run_gate "$2")"; rc=$?
  if [[ "$rc" -eq 0 ]]; then bad "$1: ACCEPTED"
  elif grep -q "^EXEMPT: $A " <<<"$out"; then bad "$1: reported EXEMPT"
  elif ! grep -q "cert holds constant $A" <<<"$out"; then
    bad "$1: failed for another reason -- $(grep -m1 '^FAIL' <<<"$out")"
  else ok "$1"; fi
}

# --- 1. FULL EXTENSION: out_w > dep0_w, amount = dep0_w ------------------
mk_cert "$TMP/full.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }"
expect_pass "full extension (out 512 > dep0 $A), amount = dep0 width" "$TMP/full.lean"

# --- 2. TRUNCATING Sext: out_w < dep0_w, amount = out_w ------------------
mk_cert "$TMP/trunc.lean" \
"      SourceDesc.flopQ 0 512
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := $A, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := $A }"
expect_pass "truncating Sext (out $A < dep0 512), amount = out width" "$TMP/trunc.lean"

# --- 3. a WRONG amount is a real disagreement ---------------------------
mk_cert "$TMP/wrong.lean" \
"      SourceDesc.flopQ 0 299
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }"
expect_fail "wrong amount ($A != min(512, 299))" "$TMP/wrong.lean"

# --- 4. used as the DATA operand (dependency 0) -------------------------
mk_cert "$TMP/dep0.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.const 9 ((Int.ofNat 9))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }"
expect_fail "used as dependency 0 (the data operand)" "$TMP/dep0.lean"

# --- 5. MIXED use: one slot a Sext amount, another slot an ordinary op --
mk_cert "$TMP/mixed.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 64" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }
      , { op := LGraphOp.Op_And, width := 64, deps := #[2, 3], origin := 2 }" \
"      { slot := 4, width := 512 }
      , { slot := 5, width := 64 }"
expect_fail "mixed use: one slot a Sext amount, another an Op_And operand" "$TMP/mixed.lean"

# --- 6. DUPLICATE ORDERING cannot change the result ---------------------
mk_cert "$TMP/order.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 64
    , SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_And, width := 64, deps := #[0, 1], origin := 1 }
      , { op := LGraphOp.Op_Sext, width := 512, deps := #[2, 3], origin := 2 }" \
"      { slot := 4, width := 64 }
      , { slot := 5, width := 512 }"
expect_fail "disqualifying slot FIRST, qualifying second" "$TMP/order.lean"

# --- 7. a DIRECT output read of the amount slot -------------------------
mk_cert "$TMP/out.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 1, width := 9 }
      , { slot := 2, width := 512 }"
expect_fail "amount slot also read directly by an output" "$TMP/out.lean"

# --- 8. a DIRECT flop field read of the amount slot ---------------------
mk_cert "$TMP/flop.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }" \
"      { width := 9, din := 1, enable := none, resetPin := none, resetValue := (0), resetActiveLow := false }"
expect_fail "amount slot also used as a flop din" "$TMP/flop.lean"

# --- 9. a DIRECT memory image read of the amount slot -------------------
mk_cert "$TMP/mem.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }" \
"" \
"      { aw := 4, dw := 9, nextImg := 1 }"
expect_fail "amount slot also used as a memory nextImg" "$TMP/mem.lean"

# --- 10. NO CONSUMER at all --------------------------------------------
mk_cert "$TMP/orphan.lean" \
"      SourceDesc.flopQ 0 512
    , SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 512" \
"      { op := LGraphOp.Op_And, width := 512, deps := #[0, 2], origin := 1 }" \
"      { slot := 3, width := 512 }"
expect_fail "amount slot with no consumer at all" "$TMP/orphan.lean"

# --- 11. a MALFORMED Sext shape (arity 3) -------------------------------
mk_cert "$TMP/arity.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 8" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1, 2], origin := 1 }" \
"      { slot := 3, width := 512 }"
expect_fail "Op_Sext with arity 3 (not the contracted shape)" "$TMP/arity.lean"

# --- 12. the positive constant-width invariant is untouched -------------
mk_cert "$TMP/fit.lean" \
"      SourceDesc.flopQ 0 $A
    , SourceDesc.const 2 ((Int.ofNat 8))" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[0, 1], origin := 1 }" \
"      { slot := 2, width := 512 }"
out="$(run_gate "$TMP/fit.lean")"; rc=$?
if [[ "$rc" -eq 0 ]]; then bad "the fit invariant no longer rejects const width 2 holding 8"
elif ! grep -q 'const width 2 cannot hold value 8' <<<"$out"; then
  bad "fit invariant reported something else: $(grep -m1 '^FAIL' <<<"$out")"
else ok "positive constant-width invariant still rejects width 2 holding 8"; fi

# --- 13. a NEGATIVE dependency index must not index backwards -----------
# The reported bypass: int("-1") is a valid Python index, so dependency -1
# reached the LAST source and supplied a width that satisfied min(out_w, dep0_w)
# by accident.  Here source 2 is a 300-bit flop, so the pre-fix checker exempted.
mk_cert "$TMP/negdep.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 8
    , SourceDesc.flopQ 0 $A" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[-1, 0], origin := 1 }" \
"      { slot := 3, width := 512 }"
expect_fail "negative dependency index (-1) must not wrap to the last source" "$TMP/negdep.lean"

# --- 14. ... and that rejection is NOT vacuous --------------------------
# A copy with ONLY the dense-index checks neutered must still accept the very
# same fixture, so check 13 is proved to be what rejects it.
sed -e 's/if not CANONICAL_NAT.fullmatch(tok) or int(tok) >= n_src + j:/if False:/' \
    -e 's/^        if i < 0:$/        if False:/' \
    "$GATE" > "$TMP/bypass_gate.py"
if ! grep -q 'if False:' "$TMP/bypass_gate.py"; then
  bad "could not build the bypass-neutered checker (the guards were not found)"
else
  bout="$(python3 -B "$TMP/bypass_gate.py" "$TMP/negdep.lean" --verilog "$TMP/probe.v" --require-verilog 2>&1)"
  brc=$?
  if [[ "$brc" -ne 0 ]]; then
    bad "the bypass-neutered checker also rejects the fixture, so check 13 proves nothing"
  elif ! grep -q "^EXEMPT: $A .*Op_Sext amount" <<<"$bout"; then
    bad "the bypass-neutered checker passed without taking the Sext exemption"
  else
    ok "with the dense-index checks neutered the SAME fixture is exempted (13 is not vacuous)"
  fi
fi

# --- 15. a SELF dependency (node j reading its own slot) ----------------
# Width 300 reading itself would give min(300, 300) = 300 and satisfy the
# contract, so only the dense-index check can reject this.
mk_cert "$TMP/selfdep.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 8" \
"      { op := LGraphOp.Op_Sext, width := $A, deps := #[2, 0], origin := 1 }" \
"      { slot := 2, width := $A }"
expect_fail "self dependency at index 0 (node reads its own dense slot)" "$TMP/selfdep.lean"

# --- 16. a FORWARD dependency on a later node ---------------------------
# Node 0 reads slot 4, which is node 1 and does exist, with a width that would
# satisfy the contract.  It is still not strictly earlier than node 0's slot.
mk_cert "$TMP/fwddep.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 8
    , SourceDesc.flopQ 0 $A" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[4, 0], origin := 1 }
      , { op := LGraphOp.Op_And, width := $A, deps := #[2, 2], origin := 2 }" \
"      { slot := 3, width := 512 }
      , { slot := 4, width := $A }"
expect_fail "forward dependency at index 0 (a later node's slot)" "$TMP/fwddep.lean"

# --- 17. an OUT-OF-RANGE dependency -------------------------------------
mk_cert "$TMP/oor.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.flopQ 0 $A" \
"      { op := LGraphOp.Op_Sext, width := 512, deps := #[99, 0], origin := 1 }" \
"      { slot := 2, width := 512 }"
expect_fail "out-of-range dependency index (no such slot)" "$TMP/oor.lean"

# --- 18. POSITIVE: dependency 0 is a valid EARLIER NODE -----------------
# The dense-index check must not break the legitimate case it exists to protect.
mk_cert "$TMP/nodedep.lean" \
"      SourceDesc.const 9 ((Int.ofNat $A))
    , SourceDesc.input 0 512" \
"      { op := LGraphOp.Op_And, width := $A, deps := #[1, 1], origin := 1 }
      , { op := LGraphOp.Op_Sext, width := 512, deps := #[2, 0], origin := 2 }" \
"      { slot := 3, width := 512 }"
expect_pass "dependency 0 is a valid earlier NODE (width from the node array)" "$TMP/nodedep.lean"

# --- 19. THE CONCRETE ARTIFACT, before and after the repair -------------
ART="${D4_SEXT_ARTIFACT:-$ROOT/temp/coreet_122_auth/blocks/vpu_tensorquant/lean/vpu_tensorquant_Lgraph.lean}"
VER="${D4_SEXT_VERILOG:-$ROOT/temp/coreet_122_cgen/vpu_tensorquant.v}"
if [[ ! -s "$ART" || ! -s "$VER" ]]; then
  printf 'N/A   no retained vpu_tensorquant artifact/Verilog to test\n'
else
  # A frozen copy of the checker with the Sext exemption NEUTERED proves the
  # artifact failed before this repair -- the control is not vacuous.
  sed 's/^def structural_sext_amounts(path):/def structural_sext_amounts(path):\n    return set()   # NEUTERED: pre-repair behaviour/' \
    "$GATE" > "$TMP/prefix_gate.py"
  python3 -B "$TMP/prefix_gate.py" "$ART" --verilog "$VER" --require-verilog --quiet >"$TMP/pre.txt" 2>&1
  pre_rc=$?
  python3 -B "$GATE" "$ART" --verilog "$VER" --require-verilog >"$TMP/post.txt" 2>&1
  post_rc=$?
  if [[ "$pre_rc" -eq 0 ]]; then
    bad "the pre-repair checker ACCEPTED vpu_tensorquant, so this control proves nothing"
  elif ! grep -q 'cert holds constant 256' "$TMP/pre.txt"; then
    bad "the pre-repair checker failed for another reason: $(grep -m1 '^FAIL' "$TMP/pre.txt")"
  elif [[ "$post_rc" -ne 0 ]]; then
    bad "vpu_tensorquant still fails after the repair: $(grep -m1 '^FAIL' "$TMP/post.txt")"
  elif ! grep -q 'Op_Sext amount equal to min' "$TMP/post.txt"; then
    bad "vpu_tensorquant passes but not for the structural Sext reason"
  else
    ok "vpu_tensorquant: pre-repair FAILS on constant 256, post-repair passes as a structural Sext amount"
  fi
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
