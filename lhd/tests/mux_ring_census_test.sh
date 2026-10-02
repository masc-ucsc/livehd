#!/bin/bash
# scripts/mux_ring_census.py decides whether an LGraph SCC is a mux ring that
# can never be taken.  A wrong YES would license deleting a LIVE feedback path,
# so every guard gets a case here.
#
# WHY SYNTHETIC.  Three of the guards cannot be exercised by txfmafrac_top:
#   * POLARITY.  Every ring there has predicates that are both exhaustive and
#     mutually exclusive, so "taken iff every predicate is false" and "iff
#     every predicate is true" are both unsatisfiable.  Reading the mux
#     polarity or the predicate backwards still reports 26/39.
#   * CONSTANT WIDTH.  Op_EQ compares `bv_uint` at each operand's own width, so
#     `-1` at width 2 IS 3.  That design happens to use only small non-negative
#     literals, where normalising is the identity.
#   * SEXT IDENTITY.  Its one ring sext has amount == input == output width, so
#     the widening case that is NOT the identity never appears.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../.." 2>/dev/null && pwd)"
CEN=""
for c in "${TEST_SRCDIR:-}/_main/scripts/mux_ring_census.py" "$ROOT/scripts/mux_ring_census.py" \
         "scripts/mux_ring_census.py"; do
  [ -r "$c" ] && { CEN="$c"; break; }
done
[ -n "$CEN" ] || { echo "FAIL: cannot find mux_ring_census.py"; exit 1; }

# ALWAYS a child path -- never $TEST_TMPDIR itself (see census_consistency_test).
if [ -n "${TEST_TMPDIR:-}" ]; then TD="$TEST_TMPDIR/mux_ring_census_test_runtime"
else TD="$ROOT/generated/mux_ring_census_test/runtime_tmp"; fi
rm -rf "$TD"; mkdir -p "$TD"

rc=0
fail() { echo "FAIL: $*"; rc=1; }

# node <nid> <kind> [consts] [const_bits]   -- the two const columns are
# positionally aligned by construction in the real dump, and the analyser
# verifies that, so they are written together here too.
node() {
  printf '{"t":"node","mod":"m","nid":%s,"kind":"%s","name":null,"color":null,"match":null,"src":null,"partitionable":true,"consts":%s,"const_bits":%s}\n' \
    "$1" "$2" "${3:-null}" "${4:-null}"
}
edge() { printf '{"t":"edge","mod":"m","from":"%s","to":"%s","bits":%s}\n' "$1" "$2" "${3:-8}"; }

run() { REPORT="$(python3 "$CEN" "$TD/$1" --cut lean "${@:2}" 2>&1)"; RC=$?; }
# By FIELD, not by regex: a `.*(ELIGIBLE|NOT-ELIGIBLE)` pattern reads every
# refusal as ELIGIBLE, because the greedy prefix eats the `NOT-`.
verdict() { echo "$REPORT" | awk '$1=="SCC" && $2=="#0" {print $5, ($5=="NOT-ELIGIBLE" ? $6 : "")}' | head -1; }
want() {
  [ "$(verdict)" = "$1" ] || fail "$2: expected '$1', got '$(verdict)'"
}

# A 2-bit selector root carried in a 4-bit container, narrowed by `& 3` exactly
# as txfma_f3.sv's decode is.  Each ring mux takes its FEEDBACK from the next on
# pin $fb and its other arm from outside; selector k is eq(root&3, Ck) behind a
# zext get_mask.
#   bits <nn> -> minimal unsigned width, which is what `const_bits` carries
cwidth() { if [ "$1" -le 1 ]; then echo 1; elif [ "$1" -le 3 ]; then echo 2; else echo 3; fi; }
#   $1 file  $2 feedback pin  $3.. the eq constant of each mux, in ring order
build_ring() {
  local f="$1" fb="$2"; shift 2
  local n=$# i=0 c other
  other=$(( fb == 1 ? 2 : 1 ))
  {
    node 99 or;                 edge '$x' or_99.p0 4
    node 90 and '"p0=3"' '"p0=2"'; edge or_99.p0 and_90.p0 4
    for c in "$@"; do
      i=$((i + 1))
      node $((100 + i)) mux
      node $((200 + i)) eq "\"p0=$c\"" "\"p0=$(cwidth "$c")\""
      node $((300 + i)) get_mask '"p2=-1"' '"p2=1"'
      node $((400 + i)) or
      edge "and_90.p0"                 "eq_$((200 + i)).p0"       2
      edge "eq_$((200 + i)).p0"        "get_mask_$((300 + i)).p0" 1
      edge "get_mask_$((300 + i)).p0"  "mux_$((100 + i)).p0"      2
      edge '$ext'                      "or_$((400 + i)).p0"       8
      edge "or_$((400 + i)).p0"        "mux_$((100 + i)).p$other" 8
    done
    for i in $(seq 1 "$n"); do
      edge "mux_$((100 + i)).p0" "mux_$((100 + (i % n) + 1)).p$fb" 8
    done
  } > "$TD/$f"
}

# --------------------------------------------------------------------------
# 1. the shape txfmafrac_top has: feedback on p1 (sel==0), COMPLETE decode.
# --------------------------------------------------------------------------
build_ring full_p1.jsonl 1 0 1 2 3
run full_p1.jsonl
[ "$RC" -eq 0 ] || fail "full_p1: rc=$RC -- $REPORT"
want "ELIGIBLE " "a complete decode on p1"
echo "$REPORT" | grep -q "^## ELIGIBLE 1 of 1" || fail "full_p1: count wrong -- $REPORT"

# --------------------------------------------------------------------------
# 2. THE POLARITY CASE.  Same ring, INCOMPLETE decode {0,1,2} of a 2-bit value:
#    taking the ring needs root&3 outside {0,1,2}, satisfiable at 3, so the
#    ring is LIVE.  Kills both polarity mutants and the drop-R5 mutant.
# --------------------------------------------------------------------------
build_ring partial_p1.jsonl 1 0 1 2
run partial_p1.jsonl
want "NOT-ELIGIBLE ring_reachable_at_root_value_3" \
  "an INCOMPLETE decode on p1 is a LIVE ring (a pass means a polarity is backwards)"

# --------------------------------------------------------------------------
# 3. the other mux polarity: feedback on p2 (sel==1) with MUTUALLY EXCLUSIVE
#    predicates is dead; the same two predicates on p1 are not.
# --------------------------------------------------------------------------
build_ring excl_p2.jsonl 2 0 1
run excl_p2.jsonl
want "ELIGIBLE " "mutually exclusive predicates on p2"
build_ring excl_p1.jsonl 1 0 1
run excl_p1.jsonl
case "$(verdict)" in
  "NOT-ELIGIBLE ring_reachable_at_root_value_"[23]) ;;
  *) fail "the same predicates on p1 leave the ring reachable, got '$(verdict)'" ;;
esac

# --------------------------------------------------------------------------
# 4. the enumeration cap REFUSES; it must never shortcut to eligible.
# --------------------------------------------------------------------------
run full_p1.jsonl --max-root-width 1
want "NOT-ELIGIBLE root_width_over_cap" "a root over --max-root-width"

# --------------------------------------------------------------------------
# 5. CONSTANT WIDTH.  Op_EQ compares bv_uint at each operand's own width and
#    pass.lean materialises the constant at dep_w = max over the EQ's operand
#    widths, so on a 2-bit value `eq(V,-1)` and `eq(V,3)` are the SAME
#    predicate.  Both muxes take p2, so the ring is taken when BOTH are true,
#    i.e. at root&3 == 3: LIVE.
#
#    Comparing the printed integers instead (-1 never equals a bv_uint in
#    [0,4)) makes the first predicate unsatisfiable and the ring wrongly dead.
# --------------------------------------------------------------------------
build_ring neg_alias.jsonl 2 -1 3
run neg_alias.jsonl
want "NOT-ELIGIBLE ring_reachable_at_root_value_3" \
  "eq(V,-1) and eq(V,3) are one predicate on a 2-bit V"

# 5b. and the WIDTH in that column is load-bearing, not decoration.  The same
#     ring, the same `-1`, differing only in const_bits:
#       width 1 -> dep_w = max(1,2,1) = 2 -> the constant is 3 -> reachable
#       width 3 -> dep_w = max(1,2,3) = 3 -> the constant is 7, which a value
#                  masked to 2 bits can never equal -> dead
#     (The tool cannot produce this divergence from RTL -- a front end sizes a
#     negative literal to its own natural width, so declared and intrinsic
#     agree there.  It is pinned here instead, at the consumer.)
build_ring cw_narrow.jsonl 2 -1 -1
run cw_narrow.jsonl
want "NOT-ELIGIBLE ring_reachable_at_root_value_3" "const_bits=1 makes -1 the value 3"
sed 's/"p0=-1","const_bits":"p0=1"/"p0=-1","const_bits":"p0=3"/g' "$TD/cw_narrow.jsonl" \
  > "$TD/cw_wide.jsonl"
run cw_wide.jsonl
want "ELIGIBLE " "const_bits=3 makes the same -1 the unreachable value 7"

# --------------------------------------------------------------------------
# 6. dep_w comes from the EQ's IMMEDIATE operands, not from the root the walk
#    ends at.  Here `& 3` NARROWS to 2 bits before the EQ, so dep_w is 2 and
#    `-1` is 3 -- reachable.  Normalising at the 4-bit ROOT width instead makes
#    it 15, `(root&3)==15` is unsatisfiable, and the live ring is called dead.
#    That is the wrong-YES direction, so this case is the important one.
# --------------------------------------------------------------------------
build_ring narrow_before_eq.jsonl 2 -1 -1
run narrow_before_eq.jsonl
want "NOT-ELIGIBLE ring_reachable_at_root_value_3" \
  "dep_w is the EQ operand width (2), not the root width (4)"

# --------------------------------------------------------------------------
# 7. the same point in the other direction: WIDEN to 3 bits before the EQ and
#    `-1` is 7, which a 2-bit value can never equal, so the p2 ring is dead.
#    Reading dep_w off the 2-bit root would make it 3 and call the ring live.
# --------------------------------------------------------------------------
{ node 99 or;                  edge '$x' or_99.p0 2
  node 90 and '"p0=3"' '"p0=2"';  edge or_99.p0 and_90.p0 2
  node 50 get_mask '"p2=-1"' '"p2=1"'; edge and_90.p0 get_mask_50.p0 2   # zext 2 -> 3
  node 201 eq '"p0=-1"' '"p0=1"';  edge get_mask_50.p0 eq_201.p0 3
  node 202 eq '"p0=3"'  '"p0=2"';  edge and_90.p0      eq_202.p0 2
  node 301 get_mask '"p2=-1"' '"p2=1"'; edge eq_201.p0 get_mask_301.p0 1
  node 302 get_mask '"p2=-1"' '"p2=1"'; edge eq_202.p0 get_mask_302.p0 1
  edge get_mask_301.p0 mux_101.p0 2; edge get_mask_302.p0 mux_102.p0 2
  node 101 mux; node 102 mux; node 401 or; node 402 or
  edge '$ext' or_401.p0 8; edge or_401.p0 mux_101.p1 8
  edge '$ext' or_402.p0 8; edge or_402.p0 mux_102.p1 8
  edge mux_101.p0 mux_102.p2 8; edge mux_102.p0 mux_101.p2 8
} > "$TD/widen_before_eq.jsonl"
run widen_before_eq.jsonl
want "ELIGIBLE " "widening to 3 bits before the EQ makes -1 the unreachable value 7"

# --------------------------------------------------------------------------
# 8. a constant whose width the dump could not state exactly must FAIL CLOSED.
#    `lhd tool cat` prints "?" for a value with unknown bits, and for a
#    declared width too narrow for its own value -- which pass.lean calls "a
#    lie" and refuses.  That second case is also the ONLY way a non-negative
#    literal can wrap, since dep_w >= the constant's own width otherwise.
# --------------------------------------------------------------------------
build_ring unknown_w.jsonl 2 0 1
sed 's/"p0=1","const_bits":"p0=1"/"p0=1","const_bits":"p0=?"/' "$TD/unknown_w.jsonl" > "$TD/unknown_w2.jsonl"
run unknown_w2.jsonl
want "NOT-ELIGIBLE eq_constant_width_unknown" "a '?' constant width"

# --------------------------------------------------------------------------
# 9. the metadata itself: absent, misaligned or malformed is UNSOUND, not a
#    verdict.
#
#    The ABSENT case is built on `neg_alias` -- a LIVE ring -- on purpose.
#    Stripped of const_bits, a reader that tolerates the absence and falls
#    back to comparing the raw integer finds `-1` equal to nothing in [0,4),
#    calls the ring dead, and returns a wrong ELIGIBLE.  Built on a ring that
#    is genuinely dead, the same tolerance would return the RIGHT answer and
#    the case would only be testing a diagnostic string.
# --------------------------------------------------------------------------
sed 's/,"const_bits":[^}]*}/}/' "$TD/neg_alias.jsonl" > "$TD/nocb.jsonl"
run nocb.jsonl
[ "$RC" -eq 3 ] || fail "a dump with no const_bits column must exit 3, got $RC ($(verdict))"
echo "$REPORT" | grep -q "no .const_bits. column" || fail "no const_bits: no diagnostic -- $REPORT"
sed 's/"consts":"p0=3","const_bits":"p0=2"/"consts":"p0=3","const_bits":"p2=2"/' \
  "$TD/full_p1.jsonl" > "$TD/badcb.jsonl"
run badcb.jsonl
[ "$RC" -eq 3 ] || fail "misaligned const_bits must exit 3, got $RC"
echo "$REPORT" | grep -q "not .*aligned" || fail "misaligned const_bits: no diagnostic -- $REPORT"

# 9b. a width that is not a usable number.  0 and negative matter as much as
#     garbage: they would reach `norm`/`low_mask` and make an EMPTY value
#     domain, i.e. a predicate nothing satisfies -- the wrong-YES direction.
for bad in x 0 -2; do
  sed 's/"const_bits":"p0=2"/"const_bits":"p0='"$bad"'"/' "$TD/full_p1.jsonl" \
    > "$TD/badw.jsonl"
  run badw.jsonl
  [ "$RC" -eq 3 ] || fail "const width '$bad' must exit 3, got $RC ($(verdict))"
done
echo "$REPORT" | grep -qE "not positive|neither an integer" \
  || fail "a malformed const width produced no diagnostic -- $REPORT"

# --------------------------------------------------------------------------
# 10. SEXT.  Op_Sext at output width w > n subtracts 2^n from a value whose
#     bit n-1 is set, so it is NOT the identity -- "the amount equals the input
#     width" is not enough to accept a ring member.  Same ring twice, differing
#     only in the sext's OUTPUT width.
# --------------------------------------------------------------------------
sext_ring() {   # $1 file  $2 sext output width
  { node 99 or;                 edge '$x' or_99.p0 4
    node 90 and '"p0=3"' '"p0=2"'; edge or_99.p0 and_90.p0 4
    node 201 eq '"p0=0"' '"p0=1"'; edge and_90.p0 eq_201.p0 2
    node 202 eq '"p0=1"' '"p0=1"'; edge and_90.p0 eq_202.p0 2
    node 301 get_mask '"p2=-1"' '"p2=1"'; edge eq_201.p0 get_mask_301.p0 1
    node 302 get_mask '"p2=-1"' '"p2=1"'; edge eq_202.p0 get_mask_302.p0 1
    edge get_mask_301.p0 mux_101.p0 2; edge get_mask_302.p0 mux_102.p0 2
    node 101 mux; node 102 mux; node 401 or; node 402 or
    edge '$ext' or_401.p0 8; edge or_401.p0 mux_101.p1 8
    edge '$ext' or_402.p0 8; edge or_402.p0 mux_102.p1 8
    node 60 sext '"p1=8"' '"p1=4"'
    # feedback on p2 (sel==1) with MUTUALLY EXCLUSIVE predicates, so the ring
    # is dead for a reason independent of the sext -- which is what makes the
    # sext the only variable between the two runs below.
    edge mux_101.p0 sext_60.p0 8          # amount 8 == input width 8
    edge sext_60.p0 mux_102.p2 "$2"       # ... but the OUTPUT is $2 bits
    edge mux_102.p0 mux_101.p2 8
  } > "$TD/$1"
}
sext_ring sext_id.jsonl 8
run sext_id.jsonl
want "ELIGIBLE " "sext with amount == input == output width is the identity"
sext_ring sext_wide.jsonl 16
run sext_wide.jsonl
want "NOT-ELIGIBLE ring_sext_not_a_passthrough" \
  "a WIDENING sext sign-extends and is not the identity"

# --------------------------------------------------------------------------
# 11. feedback entering a SELECTOR pin is not a data ring.
# --------------------------------------------------------------------------
{ node 10 mux; node 11 mux; node 12 or; node 13 or
  node 14 eq '"p0=0"' '"p0=1"'; node 15 get_mask '"p2=-1"' '"p2=1"'
  edge '$s' eq_14.p0 2; edge eq_14.p0 get_mask_15.p0 1
  edge get_mask_15.p0 mux_10.p0 2
  edge mux_10.p0 mux_11.p0 2
  edge mux_11.p0 mux_10.p1 8
  edge or_12.p0 mux_10.p2 8; edge '$e' or_12.p0 8
  edge or_13.p0 mux_11.p1 8; edge '$e' or_13.p0 8
} > "$TD/sel.jsonl"
run sel.jsonl
want "NOT-ELIGIBLE feedback_into_selector" "feedback into a selector pin"

# --------------------------------------------------------------------------
# 12. a ring member that is neither a mux nor a checked pass-through.
# --------------------------------------------------------------------------
{ node 20 mux; node 21 sum; node 22 eq '"p0=0"' '"p0=1"'
  node 23 get_mask '"p2=-1"' '"p2=1"'; node 24 or; node 25 and '"p0=3"' '"p0=2"'; node 26 or
  edge '$y' or_26.p0 4; edge or_26.p0 and_25.p0 4
  edge and_25.p0 eq_22.p0 2; edge eq_22.p0 get_mask_23.p0 1
  edge get_mask_23.p0 mux_20.p0 2
  edge or_24.p0 mux_20.p2 8; edge '$e' or_24.p0 8
  edge mux_20.p0 sum_21.p0 8; edge sum_21.p0 mux_20.p1 8
} > "$TD/sum.jsonl"
run sum.jsonl
want "NOT-ELIGIBLE ring_member_not_mux_or_passthrough:sum" "an unmodelled ring member"

# --------------------------------------------------------------------------
# 13. selectors over two DIFFERENT roots: the stated rule does not apply, and
#     the free-variable secondary measurement must still run.
# --------------------------------------------------------------------------
{ node 30 mux; node 31 mux
  node 32 eq '"p0=0"' '"p0=1"'; node 33 eq '"p0=0"' '"p0=1"'
  node 34 get_mask '"p2=-1"' '"p2=1"'; node 35 get_mask '"p2=-1"' '"p2=1"'
  node 36 or; node 37 or
  edge '$u' eq_32.p0 2; edge '$v' eq_33.p0 2
  edge eq_32.p0 get_mask_34.p0 1; edge eq_33.p0 get_mask_35.p0 1
  edge get_mask_34.p0 mux_30.p0 2; edge get_mask_35.p0 mux_31.p0 2
  edge or_36.p0 mux_30.p2 8; edge '$e' or_36.p0 8
  edge or_37.p0 mux_31.p2 8; edge '$e' or_37.p0 8
  edge mux_30.p0 mux_31.p1 8; edge mux_31.p0 mux_30.p1 8
} > "$TD/two_roots.jsonl"
run two_roots.jsonl
want "NOT-ELIGIBLE selectors_over_different_values" "two roots"
echo "$REPORT" | grep -q "multivar: REACHABLE with all roots free" \
  || fail "the free-variable secondary measurement did not run: $REPORT"

[ "$rc" -eq 0 ] && echo "PASS: mux_ring_census guards (polarity, const width, sext, cap, ring shape, roots)"
exit "$rc"
