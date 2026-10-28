#!/usr/bin/env bash
# Matched GENERATION-ONLY timing.  The same driver source (two ADAPTATION lines
# apart), the same certificate bytes, the same fuel, run SERIALLY.
#
# The box is shared and carries a steady background load from other users that
# cannot be removed, so the design controls for it instead of pretending it is
# absent: each block's PIN and MASTER runs are INTERLEAVED back to back and
# REPEATED, and the load average is recorded beside every run.  The reported
# figure is the MEDIAN of the repetitions.
#   measure_genprobe.sh <out-subdir> <reps> <block>...
set -uo pipefail
R="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../../.." && pwd)"   # the repo root, from this script's location
G=$R/generated/master_port/fp/genprobe
SUB=${1:?out subdir}; REPS=${2:?reps}; shift 2
O=$G/runs/$SUB; mkdir -p "$O"
PINB=$G/root-pin/nat/gen_probe
MASB=$G/root-master/nat/gen_probe
for b in "$PINB" "$MASB"; do [ -x "$b" ] || { echo "MISSING BINARY: $b"; exit 2; }; done

cert_of() {
  python3 -I -c "
import json,sys
for r in json.load(open('$G/frozen_rows.json'))['rows']:
    if r['name']==sys.argv[1]: print(r['cert'] or ''); break
" "$1"
}

run() {  # run <side> <binary> <block> <cert> <rep>
  local side=$1 bin=$2 b=$3 cert=$4 rep=$5
  local nm="$b.$side.$rep"
  local U="gen-$side-${b//[^a-zA-Z0-9]/-}-$rep-$(date +%s%N | tail -c 7)"
  printf '%s\n' "$bin" "$cert" 200000 2000 > "$O/$nm.argv"
  cut -d' ' -f1-3 /proc/loadavg > "$O/$nm.loadavg"
  systemd-run --user --scope --quiet --unit="$U" --property=Delegate=yes \
      --property=MemoryMax=8G \
      /usr/bin/time -f 'wall %e s|RSS %M KB|exit %x' -o "$O/$nm.time" \
      timeout 3600 nice -n19 ionice -c3 "$bin" "$cert" 200000 2000 > "$O/$nm.log" 2>&1
  local rc=$?
  local gen; gen=$(sed -n 's/^  GEN ms: .*TOTAL \([0-9]*\)$/\1/p' "$O/$nm.log" | head -1)
  printf '%-24s %-6s r%-2s rc=%-3s TOTAL=%-9s load=%s\n' "$b" "$side" "$rep" "$rc" "${gen:-?}" "$(cut -d' ' -f1 "$O/$nm.loadavg")"
}

for b in "$@"; do
  cert=$(cert_of "$b")
  [ -n "$cert" ] || { echo "$b: NO FROZEN CERT ROW"; continue; }
  for rep in $(seq 1 "$REPS"); do
    run pin    "$PINB" "$b" "$cert" "$rep"
    run master "$MASB" "$b" "$cert" "$rep"
  done
done
echo "MEASURE DONE: $SUB"
