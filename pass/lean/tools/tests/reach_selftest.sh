#!/usr/bin/env bash
set -euo pipefail
# Self-test of the reachability checker `ckpt_reach check` (pass/lean/tools/drivers/ckpt_reach.lean,
# HDLProof/Reach/Defs.lean: checkReach_sound).
#
# Fixtures, through the real flow (slang reader -> LGraph -> DCERT4):
#   even   a 4-bit counter that adds 2 when `en` is high, power-on value 0
#   ram    an 8-bit counter with power-on value 5 and a 12-word RAM (writes on `we`)
# Checks:
#   even   c = 6 at cycle 3 is REACHABLE with en = 1; the witness en = 0 is NOT MATCHED and names
#          flop 0; with emit: the theorem file compiles and its axioms are the standard three
#          plus one native_decide; c = 6 at cycle 4 is NOT MATCHED by en = 1 (it reaches 8)
#   ram    a checkpoint cert_trace wrote at cycle 7 is REACHABLE on the same stimulus; one
#          edited word is NOT MATCHED and named; a run from that checkpoint (init:ckpt:) to the
#          end checkpoint is REACHABLE
#   errors another design's checkpoint (exit 4), a start after the target (exit 2), a
#          stimulus that does not parse (exit 2)
#   init   sym_verify's start state (evenpow: a counter that adds 2, power-on value 1): "c is
#          even" is PROVED from the all-zero start and from a checkpoint with c = 4, and has a
#          base COUNTEREXAMPLE from the power-on state and from a checkpoint with c = 3; "c is
#          odd" is PROVED from power-on, and its emitted theorem file (init:reset) compiles, as
#          does the one from the c = 4 checkpoint; c = 4 at cycle 2 is REACHABLE from all-zero
#          and NOT MATCHED by the same witness from power-on
#   unreach the invariant excludes a checkpoint (sym_verify unreach:, not_reachable_of_verdict): c = 3
#          is UNREACHABLE from all-zero ("c is even"), c = 4 from power-on ("c is odd"), both
#          theorem files prove `unreachable`; c = 4 from all-zero is not concluded
#
# CK_NAT=<dir with native ckpt_reach, lhd_ckpt, cert_trace and sym_verify (with cadical beside
# it)> uses them; otherwise the drivers run interpreted against formal/lean/.olean-dev (build it
# with formal/lean/.build-proj.sh).

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/reach_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CK_NAT="${CK_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

cat > "$OUT/even.v" <<'EOF'
module even(input clk, input en, output [3:0] c_o);
  reg [3:0] c = 4'd0;
  always @(posedge clk) if (en) c <= c + 4'd2;
  assign c_o = c;
endmodule
EOF
cat > "$OUT/ram.v" <<'EOF'
module ram(input clk, input we, input [3:0] wa, input [7:0] wd, input [3:0] ra,
           output [7:0] c_o, output [7:0] rd_o);
  reg [7:0] c = 8'd5;
  reg [7:0] m [0:11];
  always @(posedge clk) begin
    c <= c + 8'd1;
    if (we) m[wa] <= wd;
  end
  assign c_o = c;
  assign rd_o = m[ra];
endmodule
EOF
cat > "$OUT/evenpow.v" <<'EOF'
module evenpow(input clk, input en, output [3:0] c_o, output odd_o, output is5_o);
  reg [3:0] c = 4'd1;
  always @(posedge clk) if (en) c <= c + 4'd2;
  assign c_o = c;
  assign odd_o = c[0];
  assign is5_o = (c == 4'd5);
endmodule
EOF
for d in even ram evenpow; do
  (cd "$OUT" && "$LHD" compile "$d.v" --reader slang --top "$d" --emit-dir "lg:${d}_lg" --workdir "${d}_cw" \
     > "$d.compile.log" 2>&1) || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  (cd "$OUT" && "$LHD" compile "lg:${d}_lg" --top "$d" --workdir "${d}_lw" --emit-dir "lean:${d}_lean" \
     --set formal.lean.mode=verified_compiler > "$d.lean.log" 2>&1) \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done
EVEN="$OUT/even_lean/even.dcert"
RAM="$OUT/ram_lean/ram.dcert"

N=0
drv() { # <native name> <source> args... -> RC, LOG
  local name=$1 src=$2; shift 2
  LOG="$OUT/run_$(( ++N )).log"
  if [[ -n "$CK_NAT" ]]; then
    "$CK_NAT/$name" "$@" > "$LOG" 2>&1 && RC=0 || RC=$?
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$src" "$@") > "$LOG" 2>&1 && RC=0 || RC=$?
  fi
}
reach() { drv ckpt_reach "$ROOT/pass/lean/tools/drivers/ckpt_reach.lean" "$@"; }
ck() { drv lhd_ckpt "$ROOT/pass/lean/tools/drivers/lhd_ckpt.lean" "$@"; }
ct() { drv cert_trace "$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean" "$@"; }
sv() { drv sym_verify "$ROOT/pass/lean/tools/drivers/sym_verify.lean" "$@"; }
expect() { # <exit code> <text> <driver fn> args...
  local want=$1 text=$2 fn=$3; shift 3
  "$fn" "$@"
  if [[ "$RC" != "$want" ]] || { [[ -n "$text" ]] && ! grep -qF -- "$text" "$LOG"; }; then
    cat "$LOG"; echo "FAIL: $fn $* -- expected exit $want and '$text', got exit $RC"; exit 1
  fi
}
# a hand-made checkpoint: <in> <out> <awk program>, then seal
handmade() { awk "$3" "$1" | grep -v '^digest ' > "$2.tmp"; expect 0 "" ck seal "$2.tmp" "$2"; }

# even: the input ordinals are clk 0, en 1
printf 'stim v1\nin 1 1 1\n' > "$OUT/en1.stim"
printf 'stim v1\nin 1 1 0\n' > "$OUT/en0.stim"
expect 0 "" ck init "$EVEN" "$OUT/even0.ckpt"
handmade "$OUT/even0.ckpt" "$OUT/even6at3.ckpt" '/^cycle /{print "cycle 3"; next} /^flops /{f=NR} NR==f+1 && f{$2="6"} 1'
handmade "$OUT/even0.ckpt" "$OUT/even6at4.ckpt" '/^cycle /{print "cycle 4"; next} /^flops /{f=NR} NR==f+1 && f{$2="6"} 1'
expect 0 "RESULT: REACHABLE (cycle 3, 3 steps" reach check "$EVEN" "$OUT/even6at3.ckpt" "$OUT/en1.stim" emit:"$OUT/EvenReach.lean"
expect 1 "RESULT: NOT MATCHED: flop 0 is 0x0 after the witness, the checkpoint says 0x6" \
  reach check "$EVEN" "$OUT/even6at3.ckpt" "$OUT/en0.stim"
expect 1 "RESULT: NOT MATCHED: flop 0 is 0x8 after the witness, the checkpoint says 0x6" \
  reach check "$EVEN" "$OUT/even6at4.ckpt" "$OUT/en1.stim"
# the theorem file compiles, and rests on the standard axioms plus one native_decide
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean "$OUT/EvenReach.lean") > "$OUT/emit.log" 2>&1 \
  || { cat "$OUT/emit.log"; echo "FAIL: the emitted theorem file does not compile"; exit 1; }
grep -q "'CkptReach.EvenReach.reachable' depends on axioms" "$OUT/emit.log" \
  && grep -q 'propext' "$OUT/emit.log" && grep -q 'native_decide' "$OUT/emit.log" \
  && ! grep -q 'sorryAx' "$OUT/emit.log" \
  || { cat "$OUT/emit.log"; echo "FAIL: theorem file axioms"; exit 1; }

# ram: the input ordinals are clk 0, ra 1, wa 2, wd 3, we 4
cat > "$OUT/ram.stim" <<'EOF'
stim v1
in 4 1 1
in 2 4 5
in 3 8 33
at 0 in 2 4 3
at 0 in 3 8 90
at 1 in 2 4 11
at 1 in 3 8 119
at 2 in 2 4 13
at 2 in 3 8 153
at 9 in 2 4 2
at 9 in 3 8 200
EOF
expect 0 "" ct "$RAM" 14 "$OUT/ram.stim" ckpt-at:7:"$OUT/ram7.ckpt" ckpt-out:"$OUT/ram14.ckpt"
expect 0 "RESULT: REACHABLE (cycle 7" reach check "$RAM" "$OUT/ram7.ckpt" "$OUT/ram.stim"
handmade "$OUT/ram7.ckpt" "$OUT/ram7w.ckpt" '/^mem /{h=NR} NR==h+1 && h{$2="ee"} 1'
expect 1 "RESULT: NOT MATCHED: memory 0 word 0x3 is 0x5a after the witness, the checkpoint says 0xee" \
  reach check "$RAM" "$OUT/ram7w.ckpt" "$OUT/ram.stim"
expect 0 "RESULT: REACHABLE (cycle 14, 7 steps" reach check "$RAM" "$OUT/ram14.ckpt" "$OUT/ram.stim" init:ckpt:"$OUT/ram7.ckpt"

# errors
expect 4 "RESULT: CHECKPOINT REFUSED" reach check "$RAM" "$OUT/even6at3.ckpt" "$OUT/ram.stim"
expect 2 "after the target's cycle" reach check "$RAM" "$OUT/ram7.ckpt" "$OUT/ram.stim" init:ckpt:"$OUT/ram14.ckpt"
printf 'stim v2\n' > "$OUT/bad.stim"
expect 2 'expected "stim v1"' reach check "$EVEN" "$OUT/even6at3.ckpt" "$OUT/bad.stim"

# sym_verify's start state
EP="$OUT/evenpow_lean/evenpow.dcert"
expect 0 "" ck init "$EP" "$OUT/ep0.ckpt"
handmade "$OUT/ep0.ckpt" "$OUT/ep4.ckpt" '/^flops /{f=NR} NR==f+1 && f{$2="4"} 1'
handmade "$OUT/ep0.ckpt" "$OUT/ep3.ckpt" '/^flops /{f=NR} NR==f+1 && f{$2="3"} 1'
expect 0 "RESULT: INVARIANT PROVED (k=1) -- runInvKOf_sound_ref: on every run from the all-zero state" \
  sv "$EP" '!odd_o' k:1 work:"$OUT/w1"
expect 0 "on every run from the all-zero state" sv "$EP" '!odd_o' k:1 init:zero work:"$OUT/w2"
expect 1 "RESULT: COUNTEREXAMPLE (base, replayed)" sv "$EP" '!odd_o' k:1 init:reset work:"$OUT/w3"
expect 0 "RESULT: INVARIANT PROVED (k=1)" sv "$EP" '!odd_o' k:1 init:ckpt:"$OUT/ep4.ckpt" emit:"$OUT/EvenC4.lean" work:"$OUT/w4"
expect 1 "RESULT: COUNTEREXAMPLE (base, replayed)" sv "$EP" '!odd_o' k:1 init:ckpt:"$OUT/ep3.ckpt" work:"$OUT/w5"
expect 0 "on every run from the power-on" sv "$EP" odd_o k:1 init:reset emit:"$OUT/OddReset.lean" work:"$OUT/w6"
expect 2 "" sv "$EP" odd_o k:1 init:ckpt:"$OUT/ram7.ckpt" work:"$OUT/w7"
# the start is part of the question: c = 4 at cycle 2 is reachable from all-zero, and the same
# witness misses it from power-on (where c stays odd, PROVED above)
handmade "$OUT/ep0.ckpt" "$OUT/ep4c2.ckpt" '/^cycle /{print "cycle 2"; next} /^flops /{f=NR} NR==f+1 && f{$2="4"} 1'
expect 0 "RESULT: REACHABLE (cycle 2, 2 steps" reach check "$EP" "$OUT/ep4c2.ckpt" "$OUT/en1.stim" init:zero
expect 1 "RESULT: NOT MATCHED: flop 0 is 0x5" reach check "$EP" "$OUT/ep4c2.ckpt" "$OUT/en1.stim" init:reset
# unreach: the proved invariant excludes a checkpoint (not_reachable_of_verdict)
expect 0 "UNREACHABLE: $OUT/ep3.ckpt -- not_reachable_of_verdict: no run from the all-zero state" \
  sv "$EP" '!odd_o' k:1 unreach:"$OUT/ep3.ckpt" emit:"$OUT/UnreachC3.lean" work:"$OUT/w8"
expect 0 "UNREACHABLE: $OUT/ep4.ckpt -- not_reachable_of_verdict: no run from the power-on" \
  sv "$EP" odd_o k:1 init:reset unreach:"$OUT/ep4.ckpt" emit:"$OUT/UnreachC4.lean" work:"$OUT/w9"
expect 0 "UNREACHABLE NOT CONCLUDED ($OUT/ep4.ckpt): the checkpoint's own state satisfies the property" \
  sv "$EP" '!odd_o' k:1 unreach:"$OUT/ep4.ckpt" work:"$OUT/w10"
grep -q '^  UNREACHABLE:' "$LOG" && { cat "$LOG"; echo "FAIL: a reachable checkpoint was called unreachable"; exit 1; }
for f in UnreachC3 UnreachC4; do
  (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean "$OUT/$f.lean") > "$OUT/$f.log" 2>&1 \
    || { cat "$OUT/$f.log"; echo "FAIL: $f.lean does not compile"; exit 1; }
  grep -q "'SymF2.$f.unreachable' depends on axioms" "$OUT/$f.log" && ! grep -q sorryAx "$OUT/$f.log" \
    || { cat "$OUT/$f.log"; echo "FAIL: $f.lean does not prove unreachable"; exit 1; }
done
for f in EvenC4 OddReset; do
  (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean "$OUT/$f.lean") > "$OUT/$f.log" 2>&1 \
    || { cat "$OUT/$f.log"; echo "FAIL: $f.lean does not compile"; exit 1; }
  grep -q "'SymF2.$f.invariant' depends on axioms" "$OUT/$f.log" && ! grep -q sorryAx "$OUT/$f.log" \
    || { cat "$OUT/$f.log"; echo "FAIL: $f.lean axioms"; exit 1; }
done

echo "PASS reach_selftest: $N driver runs"
