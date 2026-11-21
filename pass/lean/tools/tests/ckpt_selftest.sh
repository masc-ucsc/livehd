#!/usr/bin/env bash
set -euo pipefail
# Self-test of `lhdcert-ckpt v1` checkpoints: the library
# (formal/lean/LeanSemanticPrimitives/Compiler/Ckpt.lean) and its driver lhd_ckpt
# (pass/lean/tools/drivers/lhd_ckpt.lean), on certificates lhd exports.
#
# Fixtures, through the real flow (slang reader -> LGraph -> DCERT4):
#   pw     an 8-bit counter with a power-on value and no reset (`reg [7:0] c = 8'd5`) and a
#          12-word RAM (4-bit address) whose power-on image is a $readmemh file
#   cnt4   a 4-bit counter: another design, for the design-hash refusal
#   tc     two clock domains, an asynchronous active-low reset, a ROM loaded by $readmemh
# Checks:
#   tests/ckpt_check.lean   the library on a hand-built certificate: round trips, a resume
#                           after 4 steps, every refusal, parse errors, ext sections,
#                           normalize, diff, and the --state text read by lgraph-sim's parser
#   init                    `init reset` and `init zero` both check OK and differ; the reset
#                           checkpoint holds the counter's 5 and the image's nonzero words
#   to-state                the lgraph-sim --state text of the power-on checkpoint
#   digest and seal         a hand edit does not parse (exit 3, digest mismatch); after `seal`
#                           it checks OK and `diff` names the edited flop
#   normalize               unsorted words, a repeated address and a word equal to the
#                           default give the sorted form; an ext section is kept
#   refusals                one per rule, each an edit then `seal` (exit 1, REFUSED: and the
#                           rule's message): design hash (cnt4's certificate), flop count,
#                           flop width, flop value, memory count, memory shape, default,
#                           a word past depth, a word value, order, a repeated default
#   exit codes              usage 2, a missing file 2
#   routes                  on pw and on tc (two clocks, an asynchronous reset, a ROM): IR
#                           and VC (cert_trace) and FP (dcert2_memroute) print the same
#                           trace and write byte-identical checkpoints at cycle 9 and at the
#                           end; a run resumed from each route's cycle-9 checkpoint on each
#                           route prints the full run's lines from cycle 9 on and the same
#                           final checkpoint; a refused checkpoint stops both drivers
#
# CK_NAT=<dir with native lhd_ckpt, cert_trace and dcert2_memroute> uses them; otherwise the
# drivers run interpreted against formal/lean/.olean-dev (build it with
# formal/lean/.build-proj.sh). The Lean toolchain pin lives in formal/lean.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/ckpt_selftest}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CK_NAT="${CK_NAT:-}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
rm -rf -- "$OUT"
mkdir -p "$OUT"

# 1. the library
(cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$ROOT/pass/lean/tools/tests/ckpt_check.lean") \
  > "$OUT/ckpt_check.log" 2>&1 || true
grep -q '^PASS ckpt_check' "$OUT/ckpt_check.log" || { cat "$OUT/ckpt_check.log"; echo "FAIL: ckpt_check.lean"; exit 1; }

# 2. fixtures
cat > "$OUT/pw.v" <<'EOF'
module pw(input clk, input we, input [3:0] wa, input [7:0] wd, input [3:0] ra,
          output [7:0] c_o, output [7:0] rd_o);
  reg [7:0] c = 8'd5;
  reg [7:0] m [0:11];
  initial $readmemh("pw.hex", m);
  always @(posedge clk) begin
    c <= c + 8'd1;
    if (we) m[wa] <= wd;
  end
  assign c_o = c;
  assign rd_o = m[ra];
endmodule
EOF
cat > "$OUT/pw.hex" <<'EOF'
3a c5 00 ff
10 22 34 46
00 00 5d 6f
EOF
cat > "$OUT/cnt4.v" <<'EOF'
module cnt4(input clk, output [3:0] c_o);
  reg [3:0] c = 4'd0;
  always @(posedge clk) c <= c + 4'd1;
  assign c_o = c;
endmodule
EOF
# two clock domains, an asynchronous active-low reset, a ROM ($readmemh, never written)
cat > "$OUT/tc.v" <<'EOF'
module tc(input clka, input clkb, input rst_n, input [2:0] ra, input [7:0] d,
          output [7:0] qa, output [7:0] qb, output [7:0] rom_o);
  reg [7:0] a;
  reg [7:0] b = 8'h3c;
  reg [7:0] rom [0:5];
  initial $readmemh("tc.hex", rom);
  always @(posedge clka or negedge rst_n) if (!rst_n) a <= 8'h11; else a <= a + d;
  always @(posedge clkb) b <= b ^ a;
  assign qa = a;
  assign qb = b;
  assign rom_o = rom[ra];
endmodule
EOF
printf '01 23 45 67 89 ab\n' > "$OUT/tc.hex"
for d in pw cnt4 tc; do
  (cd "$OUT" && "$LHD" compile "$d.v" --reader slang --top "$d" --emit-dir "lg:${d}_lg" --workdir "${d}_cw" \
     > "$d.compile.log" 2>&1) || { tail -5 "$OUT/$d.compile.log"; echo "FAIL: compile $d"; exit 1; }
  (cd "$OUT" && "$LHD" compile "lg:${d}_lg" --top "$d" --workdir "${d}_lw" --emit-dir "lean:${d}_lean" \
     --set formal.lean.mode=verified_compiler > "$d.lean.log" 2>&1) \
    || { grep -o '"message":"[^"]*' "$OUT/$d.lean.log" | head -3; echo "FAIL: certificate $d"; exit 1; }
done
PW="$OUT/pw_lean/pw.dcert"
CNT="$OUT/cnt4_lean/cnt4.dcert"
[[ -f "$PW" && -f "$CNT" ]] || { ls "$OUT"/*_lean; echo "FAIL: no certificate files"; exit 1; }

# 3. the driver
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
ck() { drv lhd_ckpt "$ROOT/pass/lean/tools/drivers/lhd_ckpt.lean" "$@"; }
ct() { drv cert_trace "$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean" "$@"; }
mr() { drv dcert2_memroute "$ROOT/formal/lean/Futamura_Projection_Scripts/dcert2_memroute.lean" "$@"; }
expect() { # <exit code> <text> args...
  local want=$1 text=$2; shift 2
  ck "$@"
  if [[ "$RC" != "$want" ]] || { [[ -n "$text" ]] && ! grep -qF -- "$text" "$LOG"; }; then
    cat "$LOG"; echo "FAIL: lhd_ckpt $* -- expected exit $want and '$text', got exit $RC"; exit 1
  fi
}
# edit <in> <out> <awk program>: a hand edit; the digest line is dropped, `seal` adds it back
edit() { awk "$3" "$1" | grep -v '^digest ' > "$2"; }
sealed() { # <in> <out>: seal, which must succeed
  expect 0 "" seal "$1" "$2"
}

C=$OUT/c; mkdir -p "$C"
expect 0 "" init "$PW" "$C/reset.ckpt"
expect 0 "" init "$PW" "$C/zero.ckpt" zero
expect 0 "OK: cycle 0, 1 flops, 1 memories, 0 ext sections" check "$PW" "$C/reset.ckpt"
expect 0 "OK: cycle 0" check "$PW" "$C/zero.ckpt"
expect 1 "DIFFER: flop 0: 8'h5 vs 8'h0" diff "$C/reset.ckpt" "$C/zero.ckpt"
expect 0 "SAME" diff "$C/reset.ckpt" "$C/reset.ckpt"
# the reset checkpoint: the counter's power-on value, then the image's nonzero words
want_reset='flops 1
8 5
mems 1
mem 4 8 12 0 9
0 3a
1 c5
3 ff
4 10
5 22
6 34
7 46
a 5d
b 6f
exts 0'
got=$(sed -n '/^flops /,/^exts /p' "$C/reset.ckpt")
[[ "$got" == "$want_reset" ]] || { echo "$got"; echo "FAIL: init reset contents"; exit 1; }
grep -qx 'mem 4 8 12 0 0' "$C/zero.ckpt" || { cat "$C/zero.ckpt"; echo "FAIL: init zero memory"; exit 1; }

# to-state
expect 0 "" to-state "$PW" "$C/reset.ckpt" "$C/reset.state"
for l in 'flop 0 5' 'mem 0 default 0' 'mem 0 0 58' 'mem 0 3 255' 'mem 0 11 111'; do
  grep -qx "$l" "$C/reset.state" || { cat "$C/reset.state"; echo "FAIL: to-state lacks '$l'"; exit 1; }
done
[[ $(grep -c '^mem 0 [0-9]' "$C/reset.state") == 9 ]] || { echo "FAIL: to-state word count"; exit 1; }

# digest and seal
awk '/^flops /{f=NR} NR==f+1 && f{$2="7"} 1' "$C/reset.ckpt" > "$C/edited.ckpt"
expect 3 "digest mismatch" check "$PW" "$C/edited.ckpt"
sealed "$C/edited.ckpt" "$C/edited_sealed.ckpt"
expect 0 "OK: cycle 0" check "$PW" "$C/edited_sealed.ckpt"
expect 1 "DIFFER: flop 0: 8'h5 vs 8'h7" diff "$C/reset.ckpt" "$C/edited_sealed.ckpt"
expect 3 "not an lhdcert-ckpt v1 file" check "$PW" "$C/reset.state"
head -c 40 "$C/reset.ckpt" > "$C/truncated.ckpt"
expect 3 "ckpt:" check "$PW" "$C/truncated.ckpt"

# normalize (from the zero checkpoint, default 0), with an ext section whose payload has a newline
edit "$C/zero.ckpt" "$C/messy.ckpt" \
  '/^mem /{$6=4; print; print "b 77"; print "3 1"; print "3 5a"; print "4 0"; next}
   /^exts /{print "exts 1"; print "ext 4 host 3"; print "a"; print "b"; next} 1'
expect 0 "" normalize "$C/messy.ckpt" "$C/norm.ckpt"
edit "$C/zero.ckpt" "$C/sorted.ckpt" \
  '/^mem /{$6=2; print; print "3 5a"; print "b 77"; next}
   /^exts /{print "exts 1"; print "ext 4 host 3"; print "a"; print "b"; next} 1'
sealed "$C/sorted.ckpt" "$C/sorted_sealed.ckpt"
cmp -s "$C/norm.ckpt" "$C/sorted_sealed.ckpt" \
  || { diff "$C/norm.ckpt" "$C/sorted_sealed.ckpt" || true; echo "FAIL: normalize"; exit 1; }
expect 0 "1 ext sections" check "$PW" "$C/norm.ckpt"
sealed "$C/messy.ckpt" "$C/messy_sealed.ckpt"
expect 1 "REFUSED: " check "$PW" "$C/messy_sealed.ckpt"

# refusals: one per rule
refuse() { # <name> <message> <awk program>
  edit "$C/reset.ckpt" "$C/r_$1.ckpt" "$3"
  sealed "$C/r_$1.ckpt" "$C/r_$1_sealed.ckpt"
  expect 1 "REFUSED: " check "$PW" "$C/r_$1_sealed.ckpt"
  grep -qF -- "$2" "$LOG" || { cat "$LOG"; echo "FAIL: refusal $1 lacks '$2'"; exit 1; }
}
expect 1 "does not match the certificate's" check "$CNT" "$C/reset.ckpt"
refuse flop_count  "0 flops, the certificate declares 1"  '/^flops /{print "flops 0"; getline; next} 1'
refuse flop_width  "flop 0: width 9, the certificate declares 8"  '/^flops /{f=NR} NR==f+1 && f{$1=9} 1'
refuse flop_value  "flop 0: value 0x100 does not fit in 8 bits"  '/^flops /{f=NR} NR==f+1 && f{$2="100"} 1'
refuse mem_count   "0 memories, the certificate declares 1"  '/^mems /{print "mems 0"; s=1; next} /^exts /{s=0} !s'
refuse mem_shape   "depth=16, the certificate declares"  '/^mem /{$4=16} 1'
refuse default     "default 0x100 does not fit in 8 bits"  '/^mem /{$5="100"} 1'
refuse past_depth  "word at 0xc is past the memory's depth 12"  '/^mem /{$6=$6+1} /^exts /{print "c 1"} 1'
refuse word_value  "value 0x12c does not fit in 8 bits"  '/^mem /{h=NR} NR==h+1 && h{$2="12c"} 1'
refuse order       "not strictly increasing at 0x0"  '/^mem /{h=NR} NR==h+1 && h{w=$0; next} NR==h+2 && h{print; print w; next} 1'
refuse repeat      "repeats the default"  '/^mem /{h=NR} NR==h+1 && h{$2="0"} 1'

# exit codes
expect 2 "usage:" bogus
expect 2 "lhd_ckpt:" check "$PW" "$C/no_such.ckpt"

# 4. the routes
R=$OUT/routes; mkdir -p "$R"
CYC=20; K=9
# pw: ordinals clk 0, ra 1, wa 2, wd 3, we 4; writes at 3, 11, 13 (past depth), reads around them
cat > "$R/pw.stim" <<'EOF'
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
at 3 in 2 4 0
at 3 in 3 8 7
at 9 in 2 4 2
at 9 in 3 8 200
at 4 in 1 4 3
at 5 in 1 4 11
at 6 in 1 4 0
at 10 in 1 4 2
at 11 in 1 4 13
at 12 in 1 4 5
EOF
# tc: ordinals clka 0, clkb 1, d 2, ra 3, rst_n 4; the reset pulses at cycles 6 and 15
cat > "$R/tc.stim" <<'EOF'
stim v1
in 4 1 1
in 2 8 7
in 3 3 2
at 6 in 4 1 0
at 15 in 4 1 0
at 1 in 3 3 5
at 4 in 3 3 7
at 10 in 2 8 200
at 11 in 2 8 3
EOF
# FP's `edges:indep` schedule (domain j fires iff (k+j) % (j+2) != 0) as an `edges v1` file
{ echo "edges v1"
  for ((k = 0; k < CYC; k++)); do
    echo "at $k $(( (k % 2) != 0 ? 1 : 0 )) $(( ((k + 1) % 3) != 0 ? 1 : 0 ))"
  done; } > "$R/tc.edges"
steps() { grep '^STEP' "$1" > "$2" || true; }
for d in pw tc; do
  cert="$OUT/${d}_lean/$d.dcert"
  if [[ $d == pw ]]; then ctE="edges:third"; mrE="edges:all"; else ctE="edges:file:$R/tc.edges"; mrE="edges:indep"; fi
  for rt in ir vc; do
    ct "$cert" $CYC "$R/$d.stim" $ctE route:$rt ckpt-at:$K:"$R/$d.$rt.$K.ckpt" ckpt-out:"$R/$d.$rt.end.ckpt"
    [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: cert_trace $d route:$rt"; exit 1; }
    steps "$LOG" "$R/$d.$rt.txt"
  done
  mr "$cert" auto auto $CYC zero outputs $mrE stim:"$R/$d.stim" ckpt-at:$K:"$R/$d.fp.$K.ckpt" ckpt-out:"$R/$d.fp.end.ckpt"
  [[ $RC == 0 ]] && grep -q 'RESULT: MEMROUTE OK' "$LOG" || { tail -5 "$LOG"; echo "FAIL: dcert2_memroute $d"; exit 1; }
  steps "$LOG" "$R/$d.fp.txt"
  [[ $(wc -l < "$R/$d.ir.txt") == "$CYC" ]] || { echo "FAIL: $d: $(wc -l < "$R/$d.ir.txt") steps"; exit 1; }
  for rt in vc fp; do
    cmp -s "$R/$d.ir.txt" "$R/$d.$rt.txt" || { diff "$R/$d.ir.txt" "$R/$d.$rt.txt" | head; echo "FAIL: $d: $rt trace"; exit 1; }
    for c in $K end; do
      cmp -s "$R/$d.ir.$c.ckpt" "$R/$d.$rt.$c.ckpt" || { echo "FAIL: $d: $rt checkpoint at $c differs from ir's"; exit 1; }
    done
  done
  expect 0 "OK: cycle $K" check "$cert" "$R/$d.ir.$K.ckpt"
  sed -n "$((K + 1)),\$p" "$R/$d.ir.txt" > "$R/$d.tail.txt"
  for a in ir vc fp; do
    for b in ir vc; do
      ct "$cert" $((CYC - K)) "$R/$d.stim" $ctE route:$b ckpt-in:"$R/$d.$a.$K.ckpt" ckpt-out:"$R/$d.r.$a.$b.ckpt"
      [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: $d: resume $a on $b"; exit 1; }
      steps "$LOG" "$R/$d.r.$a.$b.txt"
    done
    mr "$cert" auto auto $((CYC - K)) ckpt:"$R/$d.$a.$K.ckpt" outputs $mrE stim:"$R/$d.stim" ckpt-out:"$R/$d.r.$a.fp.ckpt"
    [[ $RC == 0 ]] && grep -q 'RESULT: MEMROUTE OK' "$LOG" || { tail -5 "$LOG"; echo "FAIL: $d: resume $a on fp"; exit 1; }
    steps "$LOG" "$R/$d.r.$a.fp.txt"
    for b in ir vc fp; do
      cmp -s "$R/$d.r.$a.$b.txt" "$R/$d.tail.txt" \
        || { diff "$R/$d.r.$a.$b.txt" "$R/$d.tail.txt" | head; echo "FAIL: $d: resume $a on $b: trace"; exit 1; }
      cmp -s "$R/$d.r.$a.$b.ckpt" "$R/$d.ir.end.ckpt" || { echo "FAIL: $d: resume $a on $b: final checkpoint"; exit 1; }
    done
  done
done
# a refused checkpoint stops both drivers; a cycle outside the run is a usage error
ct "$PW" 5 "$R/pw.stim" ckpt-in:"$C/r_flop_value_sealed.ckpt"
[[ $RC == 1 ]] && grep -q 'does not fit in 8 bits' "$LOG" || { cat "$LOG"; echo "FAIL: cert_trace accepted a refused checkpoint"; exit 1; }
mr "$PW" auto auto 5 ckpt:"$C/r_flop_value_sealed.ckpt"
[[ $RC == 2 ]] && grep -q 'does not fit in 8 bits' "$LOG" || { cat "$LOG"; echo "FAIL: dcert2_memroute accepted a refused checkpoint"; exit 1; }
ct "$PW" 5 "$R/pw.stim" ckpt-in:"$R/pw.ir.$K.ckpt" ckpt-at:3:"$R/x.ckpt"
[[ $RC == 2 ]] && grep -q 'outside this run' "$LOG" || { cat "$LOG"; echo "FAIL: cert_trace ckpt-at outside the run"; exit 1; }
ct "$CNT" 5 "$R/pw.stim" ckpt-in:"$R/pw.ir.$K.ckpt"
[[ $RC == 1 ]] && grep -q "does not match the certificate's" "$LOG" || { cat "$LOG"; echo "FAIL: cert_trace took another design's checkpoint"; exit 1; }

echo "PASS ckpt_selftest: $N driver runs"
