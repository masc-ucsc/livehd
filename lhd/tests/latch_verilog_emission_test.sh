#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# todo/livehd/2f-latch M1 — LIVE test for latch Verilog emission + round-trip.
#
# The two bugs M1 fixed were both invisible to a yosys-only gate, which is why
# the six live prp-equiv-latch_* pairs kept PROVING while the emitter was
# broken. Each check below is chosen for what it can see that lgcheck cannot:
#
#   1. FANOUT-1 ENABLE, WHICH ONLY THE YOSYS IMPORTER PRODUCES.
#      `process_latch` read din/enable through `get_wire_or_const`, which
#      ignores pin2expr — so a single-fanout enable driver (INLINED, never given
#      a wire of its own) came out as a bare identifier declared nowhere.
#      Measured pre-fix: iverilog says "Unable to bind wire/reg/memory `eq_20'"
#      and lgcheck REFUTES the round-trip, blaming the design for a bug in the
#      emitter.
#        CRITICAL: the raw Yosys path is still the direct reproducer because it
#      wires RAW D + EN. The Pyrope/slang path initially materializes
#      `din = cond ? d : q`, but pass.cprop now canonicalizes that redundant hold
#      arm back to RAW D + EN before emission (pinned below).
#
#   2. ROUND-TRIP THROUGH OUR OWN FRONT END. The old emission was `always @*`
#      with BLOCKING `=`. yosys re-infers a latch from that (hence the passing
#      LEC), but our slang reader classifies a non-edge `always` with blocking
#      writes as plain COMBINATIONAL logic — so Verilog -> slang silently lost
#      the Latch cell. Emitting `always_latch` + `<=` is what makes the cell
#      survive, and only a slang re-read can observe it.
#
# Both directions are checked on EVERY shipped latch fixture, not just the
# reproducer, so a future emitter change cannot regress one polarity quietly.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() {
  echo "FAIL: $*"
  exit 1
}

HAVE_IVERILOG=0
command -v iverilog >/dev/null 2>&1 && HAVE_IVERILOG=1

# ---- bug 1: the yosys-importer round-trip (fanout-1 enable) ------------------
cat > "$W/raw.v" <<'EOF'
module raw(input d, input c, output logic q);
always_latch begin
  if(c == 1) begin
    q <= d;
  end
end
endmodule
EOF

"$LHD" compile "$W/raw.v" --reader yosys-verilog --top raw --recipe O1 \
  --emit verilog:"$W/raw_out.v" --workdir "$W/w_raw" -q >"$W/raw.log" 2>&1 \
  || { tail -5 "$W/raw.log"; fail "yosys-importer latch round-trip failed to compile"; }
[ -s "$W/raw_out.v" ] || fail "yosys-importer round-trip emitted no verilog"

if [ $HAVE_IVERILOG -eq 1 ]; then
  iverilog -g2012 -o /dev/null "$W/raw_out.v" 2>"$W/raw.iv" \
    || { cat "$W/raw.iv"; fail "iverilog REJECTS the yosys-importer latch emission (undeclared inlined enable — M1 bug 1)"; }
  echo "ok: yosys-importer latch emission passes iverilog -g2012"
fi

# The independent oracle. Pre-fix this REFUTED: yosys accepts the undeclared
# name as an implicit wire reading X, so the miter genuinely sees two different
# circuits. That makes lgcheck able to catch bug 1 even without iverilog.
"$LHD" lec --set formal.solver=lgyosys --impl verilog:"$W/raw_out.v" --ref verilog:"$W/raw.v" \
  --top raw --workdir "$W/w_rawchk" -q >"$W/rawchk.log" 2>&1 \
  || { tail -3 "$W/rawchk.log"; fail "yosys-importer latch round-trip is NOT equivalent to its source"; }
echo "ok: yosys-importer latch round-trip LEC-proves against its source"

# ---- bug 2 + polarity: emission form and slang round-trip --------------------
# high/low: the two polarities. `low` exercises the const-0 posclk -> `!enable`
# path, where a double negation would otherwise hide.
cat > "$W/high.prp" <<'EOF'
pub mod high(en:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  if en {
    l = d
  }
  q = l
}
EOF

cat > "$W/low.prp" <<'EOF'
pub mod low(g:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  if !g {
    l = d
  }
  q = l
}
EOF

for shape in high low; do
  out="$W/$shape.v"
  "$LHD" compile "$W/$shape.prp" --emit verilog:"$out" --workdir "$W/w_$shape" -q \
    >"$W/$shape.log" 2>&1 || { tail -5 "$W/$shape.log"; fail "$shape: compile to verilog failed"; }
  [ -s "$out" ] || fail "$shape: no verilog emitted"

  # Emission form: always_latch + nonblocking. `always @*` + `=` is what broke
  # the slang round-trip, so pin the shape directly rather than only its effect.
  grep -q "always_latch" "$out" || fail "$shape: emitted no always_latch (blocking always @* is the M1 bug)"
  grep -q "always @\*" "$out" && fail "$shape: still emits the old blocking 'always @*' latch form"
  echo "ok: $shape emits always_latch"

  # cprop canonicalization: the latch enable already supplies the hold
  # behavior, so its D input must be the real data, not `en ? data : Q`.
  # Before this rewrite cgen emitted a data mux whose false arm read `l`, plus
  # a second boolean mux on enable. Re-reading that RTL nested another hold mux
  # around it and the latch-contract checker rejected ordinary latches as
  # transparent self-updates.
  grep -Eq 'if \([^)]*\) l <= get_mask_[[:alnum:]_]+;' "$out" \
    || { cat "$out"; fail "$shape: cprop did not canonicalize latch D to raw data + enable"; }
  grep -Eq 'mux_[[:alnum:]_]+[[:space:]]*=[[:space:]]*l;' "$out" \
    && { cat "$out"; fail "$shape: emitted a redundant Q hold arm before the latch D input"; }
  echo "ok: $shape uses canonical raw D + enable (no redundant hold mux)"

  # The canonical Pyrope shape now has the same fanout-1 enable pressure as the
  # raw importer, and must still elaborate.
  if [ $HAVE_IVERILOG -eq 1 ]; then
    iverilog -g2012 -o /dev/null "$out" 2>"$W/$shape.iv" \
      || { cat "$W/$shape.iv"; fail "$shape: iverilog -g2012 REJECTS the emitted verilog (undeclared inlined driver?)"; }
    echo "ok: $shape passes iverilog -g2012"
  fi

  # Round-trip: re-read our own emission with slang and confirm the Latch cell
  # survived. The prp writer spells it `[latch=true]`; a lost latch comes back
  # as plain combinational logic with no such attribute.
  "$LHD" compile "$out" --reader slang --top "$shape" --emit pyrope:"$W/$shape.rt.prp" \
    --workdir "$W/w_${shape}_rt" -q >"$W/$shape.rt.log" 2>&1 \
    || { tail -5 "$W/$shape.rt.log"; fail "$shape: slang could not re-read the emitted verilog"; }
  grep -q "latch=true" "$W/$shape.rt.prp" \
    || { cat "$W/$shape.rt.prp"; fail "$shape: verilog -> slang LOST the Latch cell (it re-read as plain comb)"; }
  echo "ok: $shape survives verilog -> slang as a Latch"
done

# An explicit Pyrope latch written on every path is always transparent.  Tolg
# keeps that fact as enable=true (rather than making a missing control pin look
# accidental), and cprop removes the state cell after folding the enable.
cat > "$W/transparent.prp" <<'EOF'
pub mod transparent(sel:bool, d:u8) -> (q:u8@[0]) {
  reg l:u8:[latch=true]
  l = if sel { d } else { 0 }
  q = l
}
EOF

"$LHD" compile "$W/transparent.prp" --emit verilog:"$W/transparent.v" \
  --workdir "$W/w_transparent" -q >"$W/transparent.log" 2>&1 \
  || { tail -5 "$W/transparent.log"; fail "transparent: compile to verilog failed"; }
grep -q "always_comb" "$W/transparent.v" \
  || { cat "$W/transparent.v"; fail "transparent: emitted no combinational process"; }
grep -q "always_latch" "$W/transparent.v" \
  && { cat "$W/transparent.v"; fail "transparent: cprop retained a stateful latch for enable=true"; }
echo "ok: enable=true Pyrope latch canonicalizes to combinational logic"

if [ $HAVE_IVERILOG -eq 0 ]; then
  echo "note: iverilog not present — the undeclared-identifier check was SKIPPED (yosys cannot see that bug)"
fi

echo "PASS: latch_verilog_emission_test"
