#!/usr/bin/env bash
# COUNTERBALANCED matched generation timing.
#
# Why: every earlier pair ran pin first and master second, so a unanimous
# positive median cannot by itself rule out a systematic order effect.  This
# alternates A-B then B-A across repetitions, pins both sides to the SAME cpu,
# records wall AND cpu time, retains every run, and samples the cgroup guard
# that the wrapper requests so "requested" and "observed" can be told apart.
#
# QUEUEING DEFECT, recorded so it is not repeated: a first attempt queued this
# script behind a profile run with
#     while pgrep -f "root-master/nat/gen_probe" >/dev/null; do sleep 20; done
# The pattern matched the WAITER'S OWN command line, so the guard never became
# false and the batch never started.  Wait on a real child pid, or on an exact
# executable path with pgrep -x, never on a substring that appears in your own
# argv.
#
# PREDECLARED before running: 8 paired repetitions per block; repetition 1 is a
# WARMUP and is excluded from the summary but retained; cpu affinity 80 for both
# sides; fuel 200000/2000; identical certificate bytes.
#   counterbalanced.sh <out-subdir> <block>...
set -uo pipefail
R=/mada/users/czeng14/projects/livehd-pass-lean-master
G=$R/generated/master_port/fp/genprobe
CPU=80
REPS=8
SUB=${1:?subdir}; shift
O=$G/runs/$SUB; mkdir -p "$O"
echo "predeclared: reps=$REPS warmup=rep1 cpu=$CPU order=counterbalanced AB/BA fuel=200000/2000" > "$O/PREDECLARED.txt"
{ echo "governor: $(cat /sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_governor 2>/dev/null || echo n/a)"
  echo "driver:   $(cat /sys/devices/system/cpu/cpu$CPU/cpufreq/scaling_driver 2>/dev/null || echo n/a)"
  echo "nproc:    $(nproc)"; } >> "$O/PREDECLARED.txt"

cert_of() { python3 -I -c "
import json,sys
for r in json.load(open('$G/frozen_rows.json'))['rows']:
    if r['name']==sys.argv[1]: print(r['cert']); break" "$1"; }

run() { # run <side> <block> <cert> <rep> <pos>
  local side=$1 b=$2 cert=$3 rep=$4 pos=$5
  local bin=$G/root-$side/nat/gen_probe
  local nm="$b.$side.r$rep.$pos"
  local U="cb-$side-${b//[^a-zA-Z0-9]/-}-$rep-$(date +%s%N | tail -c 7)"
  cut -d' ' -f1-3 /proc/loadavg > "$O/$nm.loadavg"
  systemd-run --user --scope --quiet --unit="$U" --property=Delegate=yes \
      --property=MemoryMax=8G \
      /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss %M\nexit %x' -o "$O/$nm.time" \
      taskset -c $CPU nice -n19 ionice -c3 "$bin" "$cert" 200000 2000 > "$O/$nm.log" 2>&1 &
  local tp=$!
  # sample the delegated scope's cgroup while it exists
  for _ in $(seq 1 400); do
    local cg=/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/app.slice/$U.scope
    if [ -d "$cg" ]; then
      { echo "memory.max $(cat $cg/memory.max 2>/dev/null)"
        echo "memory.peak $(cat $cg/memory.peak 2>/dev/null)"
        grep -E '^(max|oom|oom_kill) ' $cg/memory.events 2>/dev/null | tr '\n' ' '; echo
      } > "$O/$nm.guard"; break
    fi
    sleep 0.05
  done
  wait $tp
  local g; g=$(sed -n 's/^  GEN ms: .*TOTAL \([0-9]*\)$/\1/p' "$O/$nm.log" | head -1)
  printf '%-18s %-6s r%-2s %-3s GEN=%-9s wall=%-9s user=%-8s sys=%-6s load=%s\n' \
    "$b" "$side" "$rep" "$pos" "${g:-?}" \
    "$(sed -n 's/^wall //p' "$O/$nm.time")" "$(sed -n 's/^user //p' "$O/$nm.time")" \
    "$(sed -n 's/^sys //p' "$O/$nm.time")" "$(cut -d' ' -f1 "$O/$nm.loadavg")"
}

for b in "$@"; do
  cert=$(cert_of "$b")
  for rep in $(seq 1 $REPS); do
    if [ $((rep % 2)) -eq 1 ]; then
      run pin "$b" "$cert" "$rep" 1st; run master "$b" "$cert" "$rep" 2nd
    else
      run master "$b" "$cert" "$rep" 1st; run pin "$b" "$cert" "$rep" 2nd
    fi
  done
done
echo "COUNTERBALANCED DONE: $SUB"
