#!/usr/bin/env bash
# Poor-man's sampling profiler for a gen_probe run, via gdb batch attach.
# perf is unusable here (kernel.perf_event_paranoid = 3) and valgrind/callgrind
# are not installed, so this is the available bounded method.  LIMITS, stated:
#   * gdb SIGSTOPs the process for each sample, so the PROFILED run's wall time
#     is inflated and must never be quoted as a production timing;
#   * samples are taken at whatever rate the attach allows, not a fixed
#     frequency, so counts are indicative of where time goes, not a calibrated
#     profile;
#   * -O2 without frame pointers: deep self-recursive chains unwind, but some
#     frames may be elided.
# Usage: sample_gen.sh <side> <block> <out-dir> [max-samples]
set -uo pipefail
R=/mada/users/czeng14/projects/livehd-pass-lean-master
G=$R/generated/master_port/fp/genprobe
side=${1:?side}; blk=${2:?block}; OUT=${3:?outdir}; MAX=${4:-40}
mkdir -p "$OUT"
BIN=$G/root-$side/nat/gen_probe
CERT=$(python3 -I -c "
import json,sys
for r in json.load(open('$G/frozen_rows.json'))['rows']:
    if r['name']==sys.argv[1]: print(r['cert']); break" "$blk")
[ -x "$BIN" ] || { echo "no binary $BIN"; exit 2; }
setsid nice -n19 "$BIN" "$CERT" 200000 2000 > "$OUT/$blk.$side.run.log" 2>&1 &
sleep 2
PID=$(pgrep -n -f "root-$side/nat/gen_probe")
[ -n "$PID" ] || { echo "process gone"; exit 2; }
echo "pid=$PID side=$side block=$blk" > "$OUT/$blk.$side.samples.txt"
n=0
while kill -0 "$PID" 2>/dev/null && [ "$n" -lt "$MAX" ]; do
  n=$((n+1))
  { echo "=== sample $n $(date +%s.%N) ==="
    timeout 60 gdb -p "$PID" -batch -ex "thread apply all bt 60" 2>/dev/null \
      | grep -E "^(Thread|#)"
  } >> "$OUT/$blk.$side.samples.txt"
done
wait 2>/dev/null
echo "$blk $side: $n samples -> $OUT/$blk.$side.samples.txt"
