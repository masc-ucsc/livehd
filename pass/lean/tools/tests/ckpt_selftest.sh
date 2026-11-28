#!/usr/bin/env bash
set -euo pipefail
# Self-test of `lhdcert-ckpt v1` checkpoints: the library
# (formal/lean/LeanSemanticPrimitives/Compiler/Ckpt.lean) and its driver lhd_ckpt
# (pass/lean/tools/sim/lhd_ckpt.lean), on certificates lhd exports.
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
#   routes                  on pw and on tc (two clocks, an asynchronous reset, a ROM): IR,
#                           VC, FP and REF (cert_trace) and FP (dcert2_memroute) print the same
#                           trace and write byte-identical checkpoints at cycle 9 and at the
#                           end, so FP's two drivers agree (RS-21); a run resumed from each
#                           route's cycle-9 checkpoint on each route prints the full run's lines
#                           from cycle 9 on and the same final checkpoint; a refused checkpoint
#                           stops both drivers
#   cert_run edges (RS-08)  cert_run with an edges v1 file (tc) and with edges:third (pw)
#                           prints cert_trace's lines, and its trace's edges.edges is the file;
#                           control: without the file, no edges.edges and other lines
#   dcert2_memroute options (RS-13)  the start controls (narrow refused, ramp runs), a start
#                           checkpoint, route:var and route:plain, nocompare with share:off and
#                           loop:full, trace, the default schedule, an edges v1 file,
#                           support-only, no-plain-write, an explicit budget out of fuel, a
#                           file that is not a certificate, and the refusals of a bad start,
#                           zero cycles, the retired words and a `react` stimulus line
#   tv and legacy           the per-design routes (scripts/make_tv_sim.py; make_legacy_sim.py
#                           --trace on the legacy model lhd emits from the same LGraph), on pw
#                           and tc: the same trace and byte-identical checkpoints as IR; each
#                           resumes every other route's cycle-9 checkpoint (TV: ir, vc, fp, tv)
#                           to the same lines and final checkpoint, and IR resumes theirs; the
#                           one-clock legacy model refuses a quiet cycle; legacy with compact:3
#                           prints the same lines and final checkpoint (pw is compared with
#                           IR under edges:all, which it runs; tc under the two-clock schedule)
#   init:zero (RS-07)       cert_trace (ir, vc), the TV and legacy drivers and cert_run from
#                           the all-zero start: the start checkpoint is lhd_ckpt's `init zero`,
#                           and every route prints IR's lines and writes IR's final checkpoint;
#                           control: the lines differ from the power-on run's
#   TV compaction (RS-14)   the TV driver with compact:3 prints its lines and final checkpoint;
#                           control: compact:0 compacts nothing
#   make_tv_sim (RS-15)     --reifier plain and --chunk 4 (with --name) give drivers that print
#                           IR's lines; control: a missing certificate is refused
#   legacy refusals (RS-16) make_legacy_sim.py --trace refuses an _io.json whose flop name,
#                           flop width, extra flop or missing memory the model does not match;
#                           control: the unedited one is accepted (section 5)
#   cert_trace (RS-20)      usage and an unknown option (exit 2), opt:stats, a checkpoint that
#                           does not parse (exit 2), a checkpoint's ext section noted; control:
#                           without opt:stats no vc line
#
# CK_NAT=<dir with native lhd_ckpt, cert_trace, cert_run and dcert2_memroute> uses them, and the TV and
# legacy drivers are built there with build-opt.sh (ROOT = CK_NAT's parent); otherwise every
# driver runs interpreted against formal/lean/.olean-dev (build it with
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
ck() { drv lhd_ckpt "$ROOT/pass/lean/tools/sim/lhd_ckpt.lean" "$@"; }
ct() { drv cert_trace "$ROOT/formal/lean/Futamura_Projection_Scripts/cert_trace.lean" "$@"; }
mr() { drv dcert2_memroute "$ROOT/formal/lean/Futamura_Projection_Scripts/dcert2_memroute.lean" "$@"; }
cr() { drv cert_run "$ROOT/pass/lean/tools/sim/cert_run.lean" "$@"; }
# want <exit code> <fixed text, or ""> <what>: the last run's exit code and log
want() {
  if [[ "$RC" != "$1" ]] || { [[ -n "$2" ]] && ! grep -qF -- "$2" "$LOG"; }; then
    cat "$LOG"; echo "FAIL: $3 -- expected exit $1 and '$2', got exit $RC (log $LOG)"; exit 1
  fi
}
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
  if [[ $d == pw ]]; then ctE="edges:third"; mrE="edges:third"; else ctE="edges:file:$R/tc.edges"; mrE="edges:indep"; fi
  for rt in ir vc fp ref; do
    t=$rt; [[ $rt == fp ]] && t=ctfp
    ct "$cert" $CYC "$R/$d.stim" $ctE route:$rt ckpt-at:$K:"$R/$d.$t.$K.ckpt" ckpt-out:"$R/$d.$t.end.ckpt"
    [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: cert_trace $d route:$rt"; exit 1; }
    steps "$LOG" "$R/$d.$t.txt"
  done
  mr "$cert" auto auto $CYC init:reset outputs $mrE stim:"$R/$d.stim" ckpt-at:$K:"$R/$d.fp.$K.ckpt" ckpt-out:"$R/$d.fp.end.ckpt"
  [[ $RC == 0 ]] && grep -q 'RESULT: MEMROUTE OK' "$LOG" || { tail -5 "$LOG"; echo "FAIL: dcert2_memroute $d"; exit 1; }
  steps "$LOG" "$R/$d.fp.txt"
  [[ $(wc -l < "$R/$d.ir.txt") == "$CYC" ]] || { echo "FAIL: $d: $(wc -l < "$R/$d.ir.txt") steps"; exit 1; }
  for rt in vc fp ctfp ref; do
    cmp -s "$R/$d.ir.txt" "$R/$d.$rt.txt" || { diff "$R/$d.ir.txt" "$R/$d.$rt.txt" | head; echo "FAIL: $d: $rt trace"; exit 1; }
    for c in $K end; do
      cmp -s "$R/$d.ir.$c.ckpt" "$R/$d.$rt.$c.ckpt" || { echo "FAIL: $d: $rt checkpoint at $c differs from ir's"; exit 1; }
    done
  done
  # FP's two drivers: cert_trace route:fp prints what dcert2_memroute prints
  cmp -s "$R/$d.fp.txt" "$R/$d.ctfp.txt" || { echo "FAIL: $d: cert_trace route:fp and dcert2_memroute differ"; exit 1; }
  expect 0 "OK: cycle $K" check "$cert" "$R/$d.ir.$K.ckpt"
  sed -n "$((K + 1)),\$p" "$R/$d.ir.txt" > "$R/$d.tail.txt"
  for a in ir vc fp; do
    for b in ir vc fp ref; do
      ct "$cert" $((CYC - K)) "$R/$d.stim" $ctE route:$b ckpt-in:"$R/$d.$a.$K.ckpt" ckpt-out:"$R/$d.r.$a.$b.ckpt"
      [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: $d: resume $a on $b"; exit 1; }
      steps "$LOG" "$R/$d.r.$a.$b.txt"
    done
    mr "$cert" auto auto $((CYC - K)) ckpt:"$R/$d.$a.$K.ckpt" outputs $mrE stim:"$R/$d.stim" ckpt-out:"$R/$d.r.$a.mr.ckpt"
    [[ $RC == 0 ]] && grep -q 'RESULT: MEMROUTE OK' "$LOG" || { tail -5 "$LOG"; echo "FAIL: $d: resume $a on fp"; exit 1; }
    steps "$LOG" "$R/$d.r.$a.mr.txt"
    for b in ir vc fp ref mr; do
      cmp -s "$R/$d.r.$a.$b.txt" "$R/$d.tail.txt" \
        || { diff "$R/$d.r.$a.$b.txt" "$R/$d.tail.txt" | head; echo "FAIL: $d: resume $a on $b: trace"; exit 1; }
      cmp -s "$R/$d.r.$a.$b.ckpt" "$R/$d.ir.end.ckpt" || { echo "FAIL: $d: resume $a on $b: final checkpoint"; exit 1; }
    done
  done
  # the cycle-9 checkpoints of ctfp and ref are ir's bytes (checked above), so their resumes are ir's
done
echo "ok: RS-21 cert_trace route:fp and route:ref: the same lines and checkpoints as ir and as dcert2_memroute, resumes on both"

# RS-08 cert_run with an edge schedule: an edges v1 file (tc) and edges:third (pw)
cr "$OUT/tc_lean/tc.dcert" stim:"$R/tc.stim" edges:file:"$R/tc.edges" trace:"$R/cr_tc" max:$CYC
want 0 "RESULT: limit" "cert_run tc edges:file"
cmp -s "$R/cr_tc/steps.trace" "$R/tc.ir.txt" || { diff "$R/cr_tc/steps.trace" "$R/tc.ir.txt" | head; echo "FAIL: cert_run tc edges:file: steps.trace"; exit 1; }
cmp -s "$R/cr_tc/edges.edges" "$R/tc.edges" || { diff "$R/cr_tc/edges.edges" "$R/tc.edges" | head; echo "FAIL: cert_run tc: edges.edges is not the schedule"; exit 1; }
cr "$PW" stim:"$R/pw.stim" edges:third trace:"$R/cr_pw" max:$CYC
want 0 "RESULT: limit" "cert_run pw edges:third"
cmp -s "$R/cr_pw/steps.trace" "$R/pw.ir.txt" || { diff "$R/cr_pw/steps.trace" "$R/pw.ir.txt" | head; echo "FAIL: cert_run pw edges:third: steps.trace"; exit 1; }
# control: without the schedule every clock fires: no edges.edges, other lines
cr "$OUT/tc_lean/tc.dcert" stim:"$R/tc.stim" trace:"$R/cr_tc_all" max:$CYC
want 0 "RESULT: limit" "cert_run tc, every clock"
[[ ! -e "$R/cr_tc_all/edges.edges" ]] && ! cmp -s "$R/cr_tc_all/steps.trace" "$R/tc.ir.txt" \
  || { echo "FAIL: cert_run tc without a schedule: an edges.edges, or the same lines"; exit 1; }
echo "ok: RS-08 cert_run edges:file and edges:third print cert_trace's lines; edges.edges is the schedule"

# RS-13 dcert2_memroute's options, on pw (one clock) and tc (two)
S="outputs edges:third stim:$R/pw.stim"
mr "$PW" auto auto 4 narrow
want 7 "RESULT: MEMROUTE RUNTIME POLICY REFUSED (initial state)" "dcert2_memroute narrow (under-width flops)"
mr "$PW" auto auto 4 ramp
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute ramp"
# a start checkpoint: lhd_ckpt's power-on state is init:reset; an edited one (flop 7) is another start
mr "$PW" auto auto $CYC ckpt:"$C/reset.ckpt" $S
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute from lhd_ckpt's power-on checkpoint"
cmp -s <(grep '^STEP' "$LOG") "$R/pw.fp.txt" || { echo "FAIL: dcert2_memroute from the power-on checkpoint: lines"; exit 1; }
mr "$PW" auto auto $CYC ckpt:"$C/edited_sealed.ckpt" $S
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute from an edited checkpoint"
[[ "$(grep -m1 '^STEP' "$LOG")" != "$(head -1 "$R/pw.fp.txt")" ]] || { echo "FAIL: dcert2_memroute: an edited start checkpoint changed no line"; exit 1; }
for o in route:var route:plain trace; do
  mr "$PW" auto auto $CYC init:reset $S $o
  want 0 "RESULT: MEMROUTE OK" "dcert2_memroute $o"
  cmp -s <(grep '^STEP' "$LOG") "$R/pw.fp.txt" || { echo "FAIL: dcert2_memroute $o: lines"; exit 1; }
done
grep -q '^  TRACE step ' "$LOG" || { cat "$LOG"; echo "FAIL: dcert2_memroute trace: no TRACE step line"; exit 1; }
for o in "" share:off loop:full; do
  mr "$PW" auto auto $CYC init:reset $S nocompare $o
  want 0 "RESULT: MEMROUTE EXECUTED" "dcert2_memroute nocompare $o"
  cmp -s <(grep '^STEP' "$LOG") "$R/pw.fp.txt" || { echo "FAIL: dcert2_memroute nocompare $o: lines"; exit 1; }
done
# the default schedule (no edges: word): third on one clock, indep on two
mr "$PW" auto auto $CYC init:reset outputs stim:"$R/pw.stim"
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute pw, the default schedule"
cmp -s <(grep '^STEP' "$LOG") "$R/pw.fp.txt" || { echo "FAIL: dcert2_memroute pw: the default schedule is not edges:third"; exit 1; }
for e in "" edges:file:"$R/tc.edges"; do
  mr "$OUT/tc_lean/tc.dcert" auto auto $CYC init:reset outputs stim:"$R/tc.stim" $e
  want 0 "RESULT: MEMROUTE OK" "dcert2_memroute tc ${e:-the default schedule}"
  cmp -s <(grep '^STEP' "$LOG") "$R/tc.fp.txt" || { echo "FAIL: dcert2_memroute tc ${e:-the default schedule}: lines"; exit 1; }
done
grep -q 'edges: edges v1 file' "$LOG" || { cat "$LOG"; echo "FAIL: dcert2_memroute does not name the edges file"; exit 1; }
mr "$PW" auto auto 4 init:reset support-only
want 0 "RESULT: MEMROUTE SUPPORT ONLY" "dcert2_memroute support-only"
grep -q '^STEP' "$LOG" && { echo "FAIL: dcert2_memroute support-only ran a cycle"; exit 1; }
nw=$(sed -n 's/.*Op_MemWrite nodes \([0-9]*\),.*/\1/p' "$LOG")
[[ -n "$nw" ]] || { cat "$LOG"; echo "FAIL: dcert2_memroute: no Op_MemWrite count"; exit 1; }
mr "$PW" auto auto 4 init:reset no-plain-write
if [[ $nw -gt 0 ]]; then want 6 "RESULT: MEMROUTE ADMISSION REFUSED" "dcert2_memroute no-plain-write ($nw plain writes)"
else want 0 "RESULT: MEMROUTE OK" "dcert2_memroute no-plain-write (no plain write)"; fi
mr "$PW" 1 1 4
want 3 "RESULT: MEMROUTE SPECIALIZATION OUT OF FUEL" "dcert2_memroute at fuel 1/1"
mr "$OUT/pw.v" auto auto 4
want 8 "RESULT: MEMROUTE PARSE FAILED" "dcert2_memroute on a file that is not a certificate"
mr "$PW" auto auto 4 bogus
want 2 "REFUSING: startup contract" "dcert2_memroute bogus start"
mr "$PW" auto auto 0
want 2 "cycles must be POSITIVE" "dcert2_memroute 0 cycles"
mr "$PW" auto auto 4 zero
want 2 "is now \`init:reset\`" "dcert2_memroute zero"
mr "$PW" auto auto 4 init:reset edges:all
want 2 "edges:all is now edges:third" "dcert2_memroute edges:all"
mr "$PW" auto auto 4 file:"$C/reset.ckpt"
want 2 "start from a checkpoint with \`ckpt:<file>\`" "dcert2_memroute file:"
{ cat "$R/pw.stim"; echo "react 1 0"; } > "$R/pw_react.stim"
mr "$PW" auto auto 4 init:reset stim:"$R/pw_react.stim"
want 2 "unrecognised" "dcert2_memroute with a react stimulus line"
echo "ok: RS-13 dcert2_memroute options: starts, routes, loops, trace, schedules, support-only, no-plain-write, fuel, parse, refusals"

# RS-20 cert_trace: usage, an unknown option, opt:stats, a checkpoint that does not parse, ext sections
ct
want 2 "usage: cert_trace" "cert_trace with no argument"
ct "$PW" 5 "$R/pw.stim" frob
want 2 "unknown option frob" "cert_trace frob"
ct "$PW" 5 "$R/pw.stim" route:vc opt:stats
want 0 "vc optimize: accepted" "cert_trace opt:stats"
grep -qF "vc incremental: on (event-driven, memories as tables)" "$LOG" || { cat "$LOG"; echo "FAIL: cert_trace opt:stats: the incremental line"; exit 1; }
ct "$PW" 5 "$R/pw.stim" route:vc
want 0 "" "cert_trace route:vc"
grep -q 'vc optimize\|vc incremental' "$LOG" && { cat "$LOG"; echo "FAIL: cert_trace printed vc's notes without opt:stats"; exit 1; }
ct "$PW" 5 "$R/pw.stim" ckpt-in:"$C/truncated.ckpt"
want 2 "" "cert_trace from a truncated checkpoint"
ct "$PW" 5 "$R/pw.stim" ckpt-in:"$C/norm.ckpt"
want 0 "1 ext sections not read" "cert_trace from a checkpoint with an ext section"
echo "ok: RS-20 cert_trace: usage, unknown option, opt:stats (control: none without it), unparsable checkpoint, ext note"

# 5. the per-design routes: TV (make_tv_sim.py) and legacy (make_legacy_sim.py --trace)
P=$OUT/perdesign; mkdir -p "$P"
# pd <name> <generated .lean> <extra modules> args... -> RC, LOG: the per-design driver, native or interpreted
pd() {
  local name=$1 src=$2 extra=$3; shift 3
  LOG="$OUT/run_$(( ++N )).log"
  if [[ -n "$CK_NAT" ]]; then
    "$CK_NAT/$name" "$@" > "$LOG" 2>&1 && RC=0 || RC=$?
  else
    (cd "$ROOT/formal/lean" && LEAN_PATH="$PWD/.olean-dev" lean --run "$src" "$@") > "$LOG" 2>&1 && RC=0 || RC=$?
  fi
}
pd_build() { # <generated .lean> <extra modules>
  [[ -n "$CK_NAT" ]] || return 0
  (cd "$ROOT/formal/lean" && EXTRA_MODULES="$2" OPT=-O2 ROOT="$(dirname "$CK_NAT")" \
     bash Futamura_Projection_Scripts/build-opt.sh "$1") > "$P/$(basename "$1").build.log" 2>&1 \
    || { tail -20 "$P/$(basename "$1").build.log"; echo "FAIL: build $1"; exit 1; }
}
for d in pw tc; do
  cert="$OUT/${d}_lean/$d.dcert"
  if [[ $d == pw ]]; then ctE="edges:third"; lgE="edges:all"; else ctE="edges:file:$R/tc.edges"; lgE="$ctE"; fi
  # TV
  python3 -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$cert" --out "$P/tv_$d" > "$P/tv_$d.gen.log" 2>&1 \
    || { cat "$P/tv_$d.gen.log"; echo "FAIL: make_tv_sim.py $d"; exit 1; }
  TVS="$P/tv_$d/${d}TvSim.lean"; pd_build "$TVS" Compiler/ReifyGen
  pd "${d}TvSim" "$TVS" "" $CYC "$R/$d.stim" $ctE ckpt-at:$K:"$R/$d.tv.$K.ckpt" ckpt-out:"$R/$d.tv.end.ckpt"
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: tv $d"; exit 1; }
  steps "$LOG" "$R/$d.tv.txt"
  cmp -s "$R/$d.ir.txt" "$R/$d.tv.txt" || { diff "$R/$d.ir.txt" "$R/$d.tv.txt" | head; echo "FAIL: $d: tv trace"; exit 1; }
  for c in $K end; do
    cmp -s "$R/$d.ir.$c.ckpt" "$R/$d.tv.$c.ckpt" || { echo "FAIL: $d: tv checkpoint at $c differs from ir's"; exit 1; }
  done
  for a in ir vc fp tv; do
    pd "${d}TvSim" "$TVS" "" $((CYC - K)) "$R/$d.stim" $ctE ckpt-in:"$R/$d.$a.$K.ckpt" ckpt-out:"$R/$d.r.$a.tv.ckpt"
    [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: $d: resume $a on tv"; exit 1; }
    steps "$LOG" "$R/$d.r.$a.tv.txt"
    cmp -s "$R/$d.r.$a.tv.txt" "$R/$d.tail.txt" && cmp -s "$R/$d.r.$a.tv.ckpt" "$R/$d.ir.end.ckpt" \
      || { echo "FAIL: $d: resume $a on tv"; exit 1; }
  done
  ct "$cert" $((CYC - K)) "$R/$d.stim" $ctE route:ir ckpt-in:"$R/$d.tv.$K.ckpt" ckpt-out:"$R/$d.r.tv.ir.ckpt"
  steps "$LOG" "$R/$d.r.tv.ir.txt"
  cmp -s "$R/$d.r.tv.ir.txt" "$R/$d.tail.txt" && cmp -s "$R/$d.r.tv.ir.ckpt" "$R/$d.ir.end.ckpt" \
    || { echo "FAIL: $d: resume tv on ir"; exit 1; }
  # RS-14 TV with compaction: the same lines and final checkpoint; control: compact:0 compacts nothing
  pd "${d}TvSim" "$TVS" "" $CYC "$R/$d.stim" $ctE compact:3 ckpt-out:"$R/$d.tvc.end.ckpt"
  [[ $RC == 0 ]] && grep -q '^compactions [1-9][0-9]* (every 3 cycles)$' "$LOG" || { cat "$LOG"; echo "FAIL: tv $d compact:3"; exit 1; }
  steps "$LOG" "$R/$d.tvc.txt"
  cmp -s "$R/$d.tvc.txt" "$R/$d.tv.txt" && cmp -s "$R/$d.tvc.end.ckpt" "$R/$d.tv.end.ckpt" \
    || { echo "FAIL: $d: tv compact:3 changed the trace or the final checkpoint"; exit 1; }
  pd "${d}TvSim" "$TVS" "" $CYC "$R/$d.stim" $ctE compact:0
  [[ $RC == 0 ]] && ! grep -q '^compactions' "$LOG" || { cat "$LOG"; echo "FAIL: tv $d compact:0 compacted"; exit 1; }
  # legacy: the model lhd emits from the same LGraph, with the certificate's names
  (cd "$OUT" && "$LHD" compile "lg:${d}_lg" --top "$d" --workdir "$P/${d}_legw" --emit-dir "lean:$P/${d}_leg" \
     --set formal.lean.emit_cert=false > "$P/$d.legacy.log" 2>&1) || { tail -5 "$P/$d.legacy.log"; echo "FAIL: legacy model $d"; exit 1; }
  python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$P/${d}_leg/${d}_Lgraph.lean" \
     --trace --io "$OUT/${d}_lean/${d}_io.json" --out "$P/lg_$d" > "$P/lg_$d.gen.log" 2>&1 \
    || { cat "$P/lg_$d.gen.log"; echo "FAIL: make_legacy_sim.py --trace $d"; exit 1; }
  LGS="$P/lg_$d/${d}_LegacyTrace.lean"; pd_build "$LGS" Translation/LegacyFast
  # the IR reference under the legacy model's schedule (pw: every edge fires)
  ct "$cert" $CYC "$R/$d.stim" $lgE route:ir ckpt-at:$K:"$R/$d.irl.$K.ckpt" ckpt-out:"$R/$d.irl.end.ckpt"
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: cert_trace $d $lgE"; exit 1; }
  steps "$LOG" "$R/$d.irl.txt"
  pd "${d}_LegacyTrace" "$LGS" "" "$cert" $CYC "$R/$d.stim" $lgE ckpt-at:$K:"$R/$d.lg.$K.ckpt" ckpt-out:"$R/$d.lg.end.ckpt"
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: legacy $d"; exit 1; }
  steps "$LOG" "$R/$d.lg.txt"
  cmp -s "$R/$d.irl.txt" "$R/$d.lg.txt" || { diff "$R/$d.irl.txt" "$R/$d.lg.txt" | head; echo "FAIL: $d: legacy trace"; exit 1; }
  for c in $K end; do
    cmp -s "$R/$d.irl.$c.ckpt" "$R/$d.lg.$c.ckpt" || { echo "FAIL: $d: legacy checkpoint at $c differs from ir's"; exit 1; }
  done
  # compaction (the model's memories are closures): the same lines and final checkpoint
  pd "${d}_LegacyTrace" "$LGS" "" "$cert" $CYC "$R/$d.stim" $lgE compact:3 ckpt-out:"$R/$d.lgc.end.ckpt"
  [[ $RC == 0 ]] && grep -q '^compactions [1-9][0-9]* (every 3 cycles)$' "$LOG" \
    || { cat "$LOG"; echo "FAIL: legacy $d compact:3"; exit 1; }
  steps "$LOG" "$R/$d.lgc.txt"
  cmp -s "$R/$d.lgc.txt" "$R/$d.lg.txt" && cmp -s "$R/$d.lgc.end.ckpt" "$R/$d.lg.end.ckpt" \
    || { echo "FAIL: $d: legacy compact:3 changed the trace or the final checkpoint"; exit 1; }
  sed -n "$((K + 1)),\$p" "$R/$d.irl.txt" > "$R/$d.ltail.txt"
  pd "${d}_LegacyTrace" "$LGS" "" "$cert" $((CYC - K)) "$R/$d.stim" $lgE ckpt-in:"$R/$d.irl.$K.ckpt" ckpt-out:"$R/$d.r.ir.lg.ckpt"
  steps "$LOG" "$R/$d.r.ir.lg.txt"
  cmp -s "$R/$d.r.ir.lg.txt" "$R/$d.ltail.txt" && cmp -s "$R/$d.r.ir.lg.ckpt" "$R/$d.irl.end.ckpt" \
    || { cat "$LOG"; echo "FAIL: $d: resume ir on legacy"; exit 1; }
  ct "$cert" $((CYC - K)) "$R/$d.stim" $lgE route:ir ckpt-in:"$R/$d.lg.$K.ckpt" ckpt-out:"$R/$d.r.lg.ir.ckpt"
  steps "$LOG" "$R/$d.r.lg.ir.txt"
  cmp -s "$R/$d.r.lg.ir.txt" "$R/$d.ltail.txt" && cmp -s "$R/$d.r.lg.ir.ckpt" "$R/$d.irl.end.ckpt" \
    || { echo "FAIL: $d: resume legacy on ir"; exit 1; }
  # RS-07 init:zero on every trace driver: the start is lhd_ckpt's all-zero state, and cert_trace
  # (ir, vc), TV, legacy and cert_run print the same lines and end in the same state
  expect 0 "" init "$cert" "$R/$d.zero0.ckpt" zero
  for rt in ir vc; do
    ct "$cert" $CYC "$R/$d.stim" $ctE init:zero route:$rt ckpt-at:0:"$R/$d.z.$rt.0.ckpt" ckpt-out:"$R/$d.z.$rt.end.ckpt"
    [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: cert_trace $d init:zero route:$rt"; exit 1; }
    steps "$LOG" "$R/$d.z.$rt.txt"
  done
  expect 0 "SAME" diff "$R/$d.z.ir.0.ckpt" "$R/$d.zero0.ckpt"
  pd "${d}TvSim" "$TVS" "" $CYC "$R/$d.stim" $ctE init:zero ckpt-out:"$R/$d.z.tv.end.ckpt"
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: tv $d init:zero"; exit 1; }
  steps "$LOG" "$R/$d.z.tv.txt"
  for rt in vc tv; do
    cmp -s "$R/$d.z.ir.txt" "$R/$d.z.$rt.txt" && cmp -s "$R/$d.z.ir.end.ckpt" "$R/$d.z.$rt.end.ckpt" \
      || { diff "$R/$d.z.ir.txt" "$R/$d.z.$rt.txt" | head; echo "FAIL: $d init:zero: $rt differs from ir"; exit 1; }
  done
  cr "$cert" stim:"$R/$d.stim" $ctE init:zero trace:"$R/$d.z.cr" ckpt-out:"$R/$d.z.cr.end.ckpt" max:$CYC
  want 0 "RESULT: limit" "cert_run $d init:zero"
  cmp -s "$R/$d.z.cr/steps.trace" "$R/$d.z.ir.txt" && cmp -s "$R/$d.z.cr.end.ckpt" "$R/$d.z.ir.end.ckpt" \
    || { diff "$R/$d.z.cr/steps.trace" "$R/$d.z.ir.txt" | head; echo "FAIL: $d init:zero: cert_run differs from cert_trace"; exit 1; }
  ct "$cert" $CYC "$R/$d.stim" $lgE init:zero route:ir ckpt-out:"$R/$d.z.irl.end.ckpt"
  steps "$LOG" "$R/$d.z.irl.txt"
  pd "${d}_LegacyTrace" "$LGS" "" "$cert" $CYC "$R/$d.stim" $lgE init:zero ckpt-out:"$R/$d.z.lg.end.ckpt"
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: legacy $d init:zero"; exit 1; }
  steps "$LOG" "$R/$d.z.lg.txt"
  cmp -s "$R/$d.z.irl.txt" "$R/$d.z.lg.txt" && cmp -s "$R/$d.z.irl.end.ckpt" "$R/$d.z.lg.end.ckpt" \
    || { diff "$R/$d.z.irl.txt" "$R/$d.z.lg.txt" | head; echo "FAIL: $d init:zero: legacy differs from ir"; exit 1; }
  # control: the all-zero start is another run than the power-on one
  cmp -s "$R/$d.z.ir.txt" "$R/$d.ir.txt" && { echo "FAIL: $d: init:zero prints the power-on run's lines"; exit 1; }
done
expect 1 "DIFFER: flop 0: 8'h5 vs 8'h0" diff "$C/reset.ckpt" "$R/pw.zero0.ckpt"
echo "ok: RS-07 init:zero on cert_trace (ir, vc), TV, legacy and cert_run: lhd_ckpt's zero start, the same lines and end state"
echo "ok: RS-14 the TV driver's compact:3 keeps its lines and final checkpoint; compact:0 compacts nothing"

# RS-15 make_tv_sim.py's options: the plain reifier and a chunk size, under other names
for v in "tvp pwPlain --reifier plain" "tvc pwC4 --chunk 4"; do
  set -- $v; dir=$1; nm=$2; shift 2
  python3 -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$PW" --out "$P/$dir" --name "$nm" "$@" > "$P/$dir.gen.log" 2>&1 \
    || { cat "$P/$dir.gen.log"; echo "FAIL: make_tv_sim.py $v"; exit 1; }
  src="$P/$dir/${nm}TvSim.lean"
  if [[ $dir == tvp ]]; then grep -q 'reify_design cert as tvStep$' "$src" || { echo "FAIL: --reifier plain"; exit 1; }
  else grep -q 'reify_design_chunked cert as tvStep size 4$' "$src" || { echo "FAIL: --chunk 4"; exit 1; }; fi
  if [[ -n "$CK_NAT" ]]; then
    python3 -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$PW" --out "$P/$dir" --name "$nm" "$@" --build "$(dirname "$CK_NAT")" \
      > "$P/$dir.build.log" 2>&1 && grep -q "^make_tv_sim: built " "$P/$dir.build.log" && [[ -x "$CK_NAT/${nm}TvSim" ]] \
      || { tail -20 "$P/$dir.build.log"; echo "FAIL: make_tv_sim.py $v --build"; exit 1; }
  fi
  pd "${nm}TvSim" "$src" "" $CYC "$R/pw.stim" edges:third
  [[ $RC == 0 ]] || { cat "$LOG"; echo "FAIL: tv $v"; exit 1; }
  cmp -s <(grep '^STEP' "$LOG") "$R/pw.ir.txt" || { echo "FAIL: tv $v: lines"; exit 1; }
done
python3 -I "$ROOT/pass/lean/tools/scripts/make_tv_sim.py" "$OUT/no_such.dcert" --out "$P/tvx" > "$P/tvx.gen.log" 2>&1 && RC=0 || RC=$?
[[ $RC == 2 ]] && grep -q 'no such certificate' "$P/tvx.gen.log" || { cat "$P/tvx.gen.log"; echo "FAIL: make_tv_sim.py on a missing certificate"; exit 1; }
echo "ok: RS-15 make_tv_sim.py --reifier plain, --chunk 4, --name$([[ -n "$CK_NAT" ]] && echo ", --build"): IR's lines; a missing certificate refused"

# RS-16 make_legacy_sim.py --trace refuses an _io.json the model does not match
python3 -I - "$OUT/pw_lean/pw_io.json" "$P" <<'PY'
import copy, json, sys
io = json.load(open(sys.argv[1]))
def put(name, f):
    j = copy.deepcopy(io); f(j); json.dump(j, open(f"{sys.argv[2]}/bad_{name}_io.json", 'w'))
put('name', lambda j: j['flops'][0].update(name=j['flops'][0]['name'] + '_x'))
put('width', lambda j: j['flops'][0].update(width=j['flops'][0]['width'] + 1))
put('extra', lambda j: j['flops'].append(dict(j['flops'][0], ordinal=1, name='nosuch')))
put('nomem', lambda j: j.update(memories=[]))
PY
for n in "name:no state field st_c_x" "width:is 8 bits, the certificate's 9" "extra:no state field st_nosuch" \
         "nomem:holds no certificate flop or memory"; do
  b=${n%%:*}; msg=${n#*:}
  python3 -I "$ROOT/pass/lean/Legacy_Fast_Model_Simulator/scripts/make_legacy_sim.py" "$P/pw_leg/pw_Lgraph.lean" \
    --trace --io "$P/bad_${b}_io.json" --out "$P/bad_$b" > "$P/bad_$b.log" 2>&1 && RC=0 || RC=$?
  [[ $RC == 2 ]] && grep -q -- "--trace refused" "$P/bad_$b.log" && grep -qF -- "$msg" "$P/bad_$b.log" \
    && [[ ! -e "$P/bad_$b/pw_LegacyTrace.lean" ]] || { cat "$P/bad_$b.log"; echo "FAIL: make_legacy_sim.py --trace accepted the $b edit"; exit 1; }
done
echo "ok: RS-16 make_legacy_sim.py --trace refuses 4 edited _io.json files (the unedited one is accepted above)"
pd pw_LegacyTrace "$P/lg_pw/pw_LegacyTrace.lean" "" "$PW" 5 "$R/pw.stim" edges:third
[[ $RC == 1 ]] && grep -q 'no quiet cycle' "$LOG" || { cat "$LOG"; echo "FAIL: the one-clock legacy model ran a quiet cycle"; exit 1; }

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
