#!/usr/bin/env bash
set -euo pipefail
# Self-test of the simulation drivers that need no lhd and no certificate file: they run on
# certificates built in Lean. Quick tier (run_tier.sh quick).
#
#   RS-02  dcert2_memroute --selftest: the reference comparison catches a default-only, a length
#          and a one-word difference and gives credit only to a complete comparison; the option
#          words mean cert_trace's (edges:third, init:reset) and the earlier ones (edges:all,
#          zero, file:) are refused naming the new word; the stimulus reader refuses a `react`
#          line. Exactly 21 checks, each PASS.
#   RS-03  dcert2_memroute's in-repo fixtures (full, mixed, twomem, lane) are admitted,
#          specialized, run 4 cycles and compared with the reference (MEMROUTE OK); a run of the
#          `full` fixture resumed from its own cycle-4 checkpoint prints the full run's lines from
#          cycle 4 on and writes the same final checkpoint. Controls: the ROM-write fixture is
#          refused admission (exit 6), an unknown fixture is refused (exit 2), another fixture's
#          checkpoint is refused (exit 2, the design hash).
#   RS-05  lgraph-sim's command line (formal/lean/Main.lean, the examples DirectExamples
#          carries): --list names the 10 examples and the 14 malformed ones; two-domain-counters
#          on a two-clock input trace prints the counts derived by hand from its certificate;
#          every example but the 200,000-node chain prints the same lines with --raw (the
#          unstaged step) as without; --watch-mem, --hex, --check-only, --init. Controls: the
#          same trace with domain b's reset held ends with b at 0; a malformed example exits 1
#          (refused), an unknown option and an unknown example exit 2, an unreadable input
#          trace exits 3.
#
# Every driver runs interpreted against formal/lean/.olean-dev (build it with
# formal/lean/.build-proj.sh). The Lean toolchain pin lives in formal/lean.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/master_port/quick_selftest}"
rm -rf -- "$OUT"
mkdir -p "$OUT"

N=0
drv() { # <source> args... -> RC, LOG
  local src=$1; shift
  LOG="$OUT/run_$(( ++N )).log"
  (cd "$ROOT/formal/lean" && ulimit -s unlimited && LEAN_PATH="$PWD/.olean-dev" lean --tstack=1048576 --run "$src" "$@") \
    > "$LOG" 2>&1 && RC=0 || RC=$?
}
mr() { drv "$ROOT/formal/lean/Futamura_Projection_Scripts/dcert2_memroute.lean" "$@"; }
L() { drv "$ROOT/formal/lean/Main.lean" "$@"; }
want() { # <exit code> <fixed text, or ""> <what>
  if [[ "$RC" != "$1" ]] || { [[ -n "$2" ]] && ! grep -qF -- "$2" "$LOG"; }; then
    cat "$LOG"; echo "FAIL: $3 -- expected exit $1 and '$2', got exit $RC (log $LOG)"; exit 1
  fi
}
steps() { grep '^STEP' "$1" || true; }

# RS-02 dcert2_memroute --selftest
mr --selftest
want 0 "  SELFTEST PASS" "dcert2_memroute --selftest"
np=$(grep -c '^  PASS  ' "$LOG" || true)
[[ $np == 21 ]] && ! grep -q '^  FAIL' "$LOG" || { cat "$LOG"; echo "FAIL: dcert2_memroute --selftest: $np checks passed, expected 21 and no FAIL"; exit 1; }
[[ "$(tail -1 "$LOG")" == "  SELFTEST PASS" ]] || { tail -3 "$LOG"; echo "FAIL: dcert2_memroute --selftest: the last line"; exit 1; }
echo "ok: RS-02 dcert2_memroute --selftest: 21 checks"

# RS-03 dcert2_memroute's fixtures
for f in full mixed twomem lane; do
  mr --fixture $f 200000 5000 4
  want 0 "RESULT: MEMROUTE OK" "dcert2_memroute --fixture $f"
done
F=$OUT/fixtures; mkdir -p "$F"
mr --fixture full 200000 5000 8 init:reset outputs ckpt-at:4:"$F/full4.ckpt" ckpt-out:"$F/full8.ckpt"
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute --fixture full, 8 cycles with checkpoints"
steps "$LOG" > "$F/full.txt"
[[ $(wc -l < "$F/full.txt") == 8 ]] || { cat "$LOG"; echo "FAIL: the full fixture printed $(wc -l < "$F/full.txt") STEP lines, expected 8"; exit 1; }
mr --fixture full 200000 5000 4 ckpt:"$F/full4.ckpt" outputs ckpt-out:"$F/full8r.ckpt"
want 0 "RESULT: MEMROUTE OK" "dcert2_memroute --fixture full from its cycle-4 checkpoint"
grep -q "state provenance: checkpoint $F/full4.ckpt, [0-9]* bytes, cycle 4" "$LOG" \
  || { cat "$LOG"; echo "FAIL: the resumed run does not name its checkpoint"; exit 1; }
cmp -s <(steps "$LOG") <(sed -n '5,$p' "$F/full.txt") \
  || { diff <(steps "$LOG") <(sed -n '5,$p' "$F/full.txt") || true; echo "FAIL: the resumed fixture run's lines"; exit 1; }
cmp -s "$F/full8.ckpt" "$F/full8r.ckpt" || { echo "FAIL: the resumed fixture run's final checkpoint"; exit 1; }
# controls
mr --fixture romwrite 200000 5000 4
want 6 "RESULT: MEMROUTE ADMISSION REFUSED" "dcert2_memroute --fixture romwrite (a write to a ROM)"
mr --fixture nosuch auto auto 4
want 2 "REFUSING: unknown fixture" "dcert2_memroute --fixture nosuch"
mr --fixture mixed 200000 5000 4 ckpt:"$F/full4.ckpt"
want 2 "does not match the certificate's" "dcert2_memroute --fixture mixed from the full fixture's checkpoint"
echo "ok: RS-03 dcert2_memroute fixtures: 4 run and compared, a resume from a checkpoint, 3 refusals"

# RS-05 lgraph-sim's command line
G=$OUT/lgraph; mkdir -p "$G"
printf '@10 0\n@01 0\n@11 0\n@00 0\n' > "$G/two.in"
printf '@10 1\n@01 1\n@11 1\n@00 1\n' > "$G/two_rst.in"
L --list
want 0 "" "lgraph-sim --list"
[[ $(wc -l < "$LOG") == 24 ]] && grep -q '^counter-enabled sources=' "$LOG" && grep -q '^bad-async-flag sources=' "$LOG" \
  || { cat "$LOG"; echo "FAIL: lgraph-sim --list: expected 24 lines, the examples and the malformed ones"; exit 1; }
L two-domain-counters --inputs "$G/two.in"
want 0 "" "lgraph-sim two-domain-counters"
# derived from DirectExamples.twoDomainCounters: counter a (clk_a) and counter b (clk_b), the
# outputs before each edge; the trace fires a, then b, then both, then neither
cat > "$G/two.want" <<'EOF'
# lgraph-sim two-domain-counters sources=4 nodes=2 outputs=2 flops=2 mems=0 inputs=1 clocks=2
cycle 0 out 0 0
cycle 1 out 1 0
cycle 2 out 1 1
cycle 3 out 2 2
final flop 2 2
EOF
cmp -s "$LOG" "$G/two.want" || { diff "$LOG" "$G/two.want" || true; echo "FAIL: lgraph-sim two-domain-counters"; exit 1; }
# control: domain b's synchronous reset held, so b is cleared at each of its edges
L two-domain-counters --inputs "$G/two_rst.in"
want 0 "" "lgraph-sim two-domain-counters, reset held"
[[ "$(tail -1 "$LOG")" == "final flop 2 0" ]] && ! cmp -s "$LOG" "$G/two.want" \
  || { cat "$LOG"; echo "FAIL: lgraph-sim: the held reset did not clear counter b"; exit 1; }
# the staged step (default) and the unstaged one (--raw) print the same lines
for e in comb-add-mask counter-enabled counter-async-reset rom-table mem-read-write two-domain-counters \
         async-reset-off-domain mem-off-domain chain-1k; do
  L "$e" --cycles 8 --show-state
  want 0 "" "lgraph-sim $e"
  cp "$LOG" "$G/$e.staged"
  L "$e" --cycles 8 --show-state --raw
  want 0 "" "lgraph-sim $e --raw"
  cmp -s "$LOG" "$G/$e.staged" || { diff "$LOG" "$G/$e.staged" || true; echo "FAIL: lgraph-sim $e: --raw differs"; exit 1; }
  [[ $(grep -c '^cycle [0-7] out ' "$LOG") == 8 && $(grep -c '^cycle [0-7] flop' "$LOG") == 8 ]] \
    || { cat "$LOG"; echo "FAIL: lgraph-sim $e: 8 out and 8 flop lines expected"; exit 1; }
done
L mem-read-write --cycles 4 --watch-mem 0:0
want 0 "" "lgraph-sim --watch-mem"
[[ $(grep -cE '^cycle [0-3] mem 0\[0\] ' "$LOG") == 4 ]] || { cat "$LOG"; echo "FAIL: lgraph-sim --watch-mem"; exit 1; }
L comb-add-mask --cycles 2 --hex
want 0 "" "lgraph-sim --hex"
grep '^cycle [01] out ' "$LOG" | cut -d' ' -f4- | tr ' ' '\n' | grep -qv '^0x' && { cat "$LOG"; echo "FAIL: lgraph-sim --hex"; exit 1; }
L counter-enabled --check-only
want 0 "" "lgraph-sim --check-only"
[[ "$(tail -1 "$LOG")" == ok ]] || { cat "$LOG"; echo "FAIL: lgraph-sim --check-only"; exit 1; }
L counter-enabled --cycles 3 --init zero
want 0 "" "lgraph-sim --init zero"
L counter-enabled --cycles 3 --init reset
want 0 "" "lgraph-sim --init reset"
# controls: refusals and exit codes
L bad-arity-not --cycles 1
want 1 "lgraph-sim: refused:" "lgraph-sim on a malformed example"
L --frob
want 2 "unknown option '--frob'" "lgraph-sim --frob"
L nosuch
want 2 "no certificate named 'nosuch'" "lgraph-sim nosuch"
L comb-add-mask --inputs "$G/no_such.in"
want 3 "cannot read" "lgraph-sim on a missing input trace"
echo "ok: RS-05 lgraph-sim: --list, a two-clock trace, --raw on 9 examples, --watch-mem, --hex, --check-only, --init, 5 controls"

echo "PASS quick_selftest: $N driver runs"
