#!/bin/bash
# B11 (minion_tlb) INITIALIZED-TRANSITION ACCEPTANCE GATE.
#
# `dcert2_memroute` exits 0 and reports COMPLETE for any admitted design that
# executes -- including one whose memory never changes.  So exit 0 alone cannot
# satisfy B11.  This gate reads the run log and REQUIRES the source-derived
# transition, failing if it is missing or wrong.
#
# THE EXPECTATION IS SOURCE-DERIVED, not observed-then-asserted.  It comes from
# the pinned CORE-ET DV sequence (hw/ip/minion/tlb/dv/minion_tlb_test.cc
# fill_entry, first call at :299) packed against the pinned minion_pkg.sv /
# tlb_pkg.sv structs for THIS artifact's NrMinions=1, Entries=8 configuration:
#
#   address 1, word 0x223600abcde = {req_tag_q 0x22, entry 0x3600abcde}
#
# 0x223600abcde has bits set ABOVE BIT 31, and every supplied initial word is
# (1 << 60) + address, so the change is above bit 31 on both sides.  Under the
# pre-repair one-bit slot mask only lane 0 could ever be written, so this word
# was unreachable -- which is what the negative control below pins.
set -u
LOG="${1:?usage: b11_tlb_acceptance.sh <run.log> [--expect-no-transition]}"
MODE="${2:-}"
EXP_ADDR=1
EXP_WORD=2350958427358          # 0x223600abcde
fail() { echo "FAIL: $*"; exit 1; }

[ -s "$LOG" ] || fail "run log $LOG is missing or empty"
grep -q "^PRODUCER_EXIT=0" "$LOG" || fail "the producer did not exit 0: $(grep -o 'PRODUCER_EXIT=[0-9]*' "$LOG" | tail -1)"

# the route and the production shape
grep -q "memory route: accepts true typed true sized true nextImgs true | ADMITTED true" "$LOG" \
  || fail "the memory route did not admit the design"
grep -qE "residual [0-9]+ terms, 1 function\(s\), arity \[3\], checker bound [0-9]+" "$LOG" \
  || fail "no arity-3 residual at a checked bound"
grep -q "executing .* at the checked bound" "$LOG" || fail "execution was not at the checked bound"
grep -qE "step outcomes: (ok,)*ok$" "$LOG" || fail "not every step returned ok"
grep -q "reference comparison: COMPLETE" "$LOG" || fail "the reference comparison was not COMPLETE"
grep -q "state survives encStateM/decStateM unchanged" "$LOG" || fail "the supplied state did not survive the codec"
grep -q "final returned-state policy: stateFullOKB true" "$LOG" || fail "the returned state failed the policy"
grep -q "state provenance: supplied file" "$LOG" || fail "the initial state was not SUPPLIED from a file"

INIT=$(grep -oE "initial memory 0 words \[[^]]*\]" "$LOG" | head -1 | sed 's/.*\[//;s/\]//')
FIN=$(grep -oE "final memory 0 words \[[^]]*\]" "$LOG"  | head -1 | sed 's/.*\[//;s/\]//')
[ -n "$INIT" ] && [ -n "$FIN" ] || fail "the log does not report both the initial and final image"
IFS=',' read -r -a IW <<< "$INIT"
IFS=',' read -r -a FW <<< "$FIN"
BEFORE="${IW[$EXP_ADDR]}"
AFTER="${FW[$EXP_ADDR]}"

if [ "$MODE" = "--expect-no-transition" ]; then
  [ "$AFTER" = "$EXP_WORD" ] \
    && fail "NEGATIVE CONTROL FAILED: the pre-repair certificate produced the expected word $EXP_WORD"
  echo "ok: negative control -- address $EXP_ADDR holds $AFTER, not the expected $EXP_WORD"
  echo "PASS: b11_tlb_acceptance (negative)"
  exit 0
fi

# THE TRANSITION
[ "$AFTER" = "$EXP_WORD" ] \
  || fail "address $EXP_ADDR holds $AFTER; the source-derived expectation is $EXP_WORD (0x223600abcde)"
[ "$BEFORE" != "$AFTER" ] || fail "address $EXP_ADDR did not change -- no transition was observed"
echo "ok: address $EXP_ADDR  before $BEFORE  ->  after $AFTER (= 0x223600abcde, the source-derived expectation)"

# the change must be ABOVE BIT 31, which a one-bit slot mask could not reach
HI_B=$(( BEFORE >> 32 )); HI_A=$(( AFTER >> 32 ))
[ "$HI_B" != "$HI_A" ] || fail "nothing above bit 31 changed; a low-lane-only gate could have produced this"
echo "ok: bits above 31 changed ($HI_B -> $HI_A), so the high lanes were genuinely written"

# EXACTLY one word moved: every other address is preserved
CH=$(sed -n 's/.*memory 0: \([0-9][0-9]*\) word(s) differ.*/\1/p' "$LOG" | head -1)
[ "$CH" = "1" ] || fail "expected exactly one changed word, the log reports $CH"
for i in "${!IW[@]}"; do
  [ "$i" = "$EXP_ADDR" ] && continue
  [ "${IW[$i]}" = "${FW[$i]}" ] || fail "address $i changed ${IW[$i]} -> ${FW[$i]}: quiet/disabled preservation broken"
done
echo "ok: every other address is bit-for-bit preserved (quiet/disabled preservation)"

echo "PASS: b11_tlb_acceptance"
