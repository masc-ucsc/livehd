#!/usr/bin/env bash
# COVERAGE-TABLE CELL RUNNER for the 12x5 simulator-generation matrix.
#
# One cell = one approach's own generator on one benchmark row.  This records
# metrics DURING the coverage attempt; it adds no research experiment, no
# repetition sweep and no profiling.
#
# WHOLE-RUN cgroup observation, unlike fp_gen_counterbalanced.sh: the sampler
# polls the delegated scope until the process exits and keeps the FINAL
# memory.peak and FINAL memory.events, with memory.max read back as genuine
# enforcement.  If the scope disappears before a sample lands, the record says
# so rather than promoting the requested limit into observed evidence.
#
#   fp_cov_run.sh <row-id> <approach> <cell-dir> <mem-max> <timeout-s> -- <argv...>
set -uo pipefail
ROW=${1:?row}; APP=${2:?approach}; CELL=${3:?cell dir}; MEM=${4:?memmax}; TMO=${5:?timeout}
shift 5; [ "${1:-}" = "--" ] && shift
mkdir -p "$CELL"
U="cov-${ROW}-${APP//[^a-zA-Z0-9]/}-$(date +%s%N | tail -c 7)"
CG=/sys/fs/cgroup/user.slice/user-$(id -u).slice/user@$(id -u).service/app.slice/$U.scope

{ printf '%s\n' "$@"; } > "$CELL/argv.txt"
cut -d' ' -f1-3 /proc/loadavg > "$CELL/loadavg.start"
date -Iseconds > "$CELL/started_at"

systemd-run --user --scope --quiet --unit="$U" --property=Delegate=yes \
    --property=MemoryMax="$MEM" \
    /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kb %M\nexit %x' -o "$CELL/time.txt" \
    timeout "$TMO" nice -n19 ionice -c3 "$@" > "$CELL/stdout.log" 2> "$CELL/stderr.log" &
RUNPID=$!

samples=0
: > "$CELL/cgroup.samples"
while kill -0 $RUNPID 2>/dev/null; do
  if [ -d "$CG" ]; then
    samples=$((samples+1))
    { echo "t=$(date +%s.%N)"
      echo "memory.max=$(cat $CG/memory.max 2>/dev/null)"
      echo "memory.peak=$(cat $CG/memory.peak 2>/dev/null)"
      echo "memory.current=$(cat $CG/memory.current 2>/dev/null)"
      sed 's/^/events./;s/ /=/' $CG/memory.events 2>/dev/null | tr '\n' ' '; echo
    } >> "$CELL/cgroup.samples"
  fi
  sleep 0.5
done
wait $RUNPID; rc=$?
cut -d' ' -f1-3 /proc/loadavg > "$CELL/loadavg.end"
date -Iseconds > "$CELL/ended_at"

# FINAL observation = the last sample taken while the scope still existed.
if [ "$samples" -gt 0 ]; then
  awk '/^t=/{blk=""} {blk=blk $0 "\n"} END{printf "%s", blk}' "$CELL/cgroup.samples" > "$CELL/cgroup.final"
  echo "observation_scope=final-sample-before-exit samples=$samples" >> "$CELL/cgroup.final"
else
  echo "observation_scope=NONE: the scope never appeared to the sampler; memory.max is REQUESTED only, not observed" > "$CELL/cgroup.final"
fi
echo "$rc" > "$CELL/exit"
printf '%-26s %-22s rc=%-4s %s | cgroup samples=%s\n' "$ROW" "$APP" "$rc" \
  "$(tr '\n' ' ' < "$CELL/time.txt")" "$samples"
