#!/usr/bin/env bash
# FIXTURE-ONLY check of private_compiler_build.sh's LIVE-IMPORTER guard.
#
# It extracts the REAL guard out of the build script -- so it cannot drift from
# it -- and points it at a throwaway project-local fixture root, against which it
# spawns four short-lived processes with KNOWN properties:
#
#   A  a reader holding an OPEN FD on a file inside the root (cwd outside)
#   B  a reader with the root in its ENVIRONMENT and a RELATIVE argv (cwd outside)
#   C  a MAPPED reader: a file inside the root mmap'd (cwd outside)
#   D  an UNRELATED WAITER whose argv merely names the root, holding nothing
#
# A, B and C must be detected; D must NOT be.  The previous guard generations got
# exactly these wrong: an argv grep would have flagged D and missed B, and the
# ancestor walk excluded every DESCENDANT of the invoking shell, which is what A,
# B, C and D all are here -- so under that logic the guard would have reported a
# clean context with three real readers live.
#
# Each process is killed BY PID at the end.  No pgrep pattern is used anywhere:
# a pattern that matches the waiter's own command line matches the waiter itself.
#
#   pcb_importer_guard_test.sh [scratch-dir]
set -uo pipefail
HERE=$(dirname "$(readlink -f "$0")")
SRC=$HERE/private_compiler_build.sh
[ -f "$SRC" ] || { echo "no private_compiler_build.sh beside this test"; exit 2; }
J=${1:-$HERE/../../../generated/fixture/pcb_guard_$$}
mkdir -p "$J/root/sub" || exit 2
J=$(readlink -f "$J"); ROOT=$J/root
printf 'ARTIFACT\n' > "$ROOT/sub/thing.olean"
sed -n "/^import os, sys, pathlib$/,/^print(\"\\\\n\".join(hits + unknown))$/p" "$SRC" > "$J/guard.py"
grep -q 'KNOWN OWNED CONSUMER' "$J/guard.py" || { echo "REFUSING: could not extract the guard body"; exit 2; }
echo "extracted the live guard body from $SRC ($(wc -l < "$J/guard.py") lines)"
echo "scratch: $J"

cd /                                   # every fixture process has its cwd OUTSIDE the root
exec 9<"$ROOT/sub/thing.olean"; sleep 60 <&9 & A=$!   # A: open fd
exec 9<&-
env LEAN_PATH="$ROOT/sub" sleep 60 &                  B=$!   # B: environment + relative argv
python3 -I -c "
import mmap,sys,time
f=open('$ROOT/sub/thing.olean','rb'); m=mmap.mmap(f.fileno(),0,prot=mmap.PROT_READ); time.sleep(60)" & C=$!
sh -c 'sleep 60' "$ROOT/sub/thing.olean" & D=$!       # D: unrelated waiter, argv names the root
sleep 1
echo "fixture pids: A(fd)=$A B(env)=$B C(mapped)=$C D(waiter)=$D"

out=$(python3 -I "$J/guard.py" "$ROOT" "$$" "" "" 2>/dev/null)
kill "$A" "$B" "$C" "$D" 2>/dev/null
fail=0
for spec in "A:$A:detected" "B:$B:detected" "C:$C:detected" "D:$D:absent"; do
  n=${spec%%:*}; rest=${spec#*:}; pid=${rest%%:*}; want=${rest##*:}
  if echo "$out" | grep -qE "^ *$pid  "; then got=detected; else got=absent; fi
  if [ "$got" = "$want" ]; then r=PASS; else r=FAIL; fail=1; fi
  printf '%-4s %s pid=%-8s expected=%-9s got=%s\n' "$r" "$n" "$pid" "$want" "$got"
done
echo
echo "$out" | sed 's/^/    /' | head -8
echo
if [ "$fail" = 0 ]; then echo "GUARD FIXTURE PASSES (scratch: $J)"; else echo "GUARD FIXTURE FAILURES (scratch: $J)"; fi
exit "$fail"
