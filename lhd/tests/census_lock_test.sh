#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# The census run-directory lock.
#
# INCIDENT IT PREVENTS (generated/census_d2/INCIDENTS.md): a driver was killed
# by a pattern match, its `xargs -P` was reparented to PPID 1 and kept
# generating, the directory was then rm -rf'd and recreated for a fresh run,
# and two jobs wrote the same tree for several minutes -- one of them into an
# unlinked inode.
#
# The lock must be held for the LIFE of the run and inherited by children, so
# an orphaned worker still holds it and a relaunch refuses. That inheritance is
# the whole point, so it is tested explicitly rather than assumed.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRV=""
for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_d2_census.sh" \
         "$(cd "$HERE/../.." 2>/dev/null && pwd)/scripts/coreet_d2_census.sh" \
         "scripts/coreet_d2_census.sh"; do
  [ -r "$c" ] && { DRV="$c"; break; }
done
[ -n "$DRV" ] || { echo "FAIL: cannot find coreet_d2_census.sh"; exit 1; }
command -v flock >/dev/null 2>&1 || { echo "note: no flock; SKIPPED"; echo "PASS: census_lock_test (skipped)"; exit 0; }

T="${TEST_TMPDIR:-$(cd "$HERE/../.." && pwd)/generated/tests}/census_lock"
rm -rf "$T"; mkdir -p "$T/run"; fails=0

printf 'aaa\nbbb\nccc\n' > "$T/mods.raw"
DIG="$(head -c -1 "$T/mods.raw" | sha256sum | cut -d' ' -f1)"
{ echo "# toy corpus"; echo "#   $DIG"; cat "$T/mods.raw"; } > "$T/corpus.txt"

# Hold the lock the way a live run (or its orphaned child) does.
mkdir -p "$T/run"
( exec 9>"$T/run/.lock"; flock 9; sleep 25 ) &
HOLDER=$!
# Wait for the lock to actually be taken, rather than racing it.
for _ in $(seq 1 50); do
  if ! flock -n "$T/run/.lock" true 2>/dev/null; then break; fi
  sleep 0.1
done

out="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --only aaa \
        --out "$T/run" 2>&1)"; rc=$?
if [ "$rc" -eq 0 ]; then
  echo "FAIL: the driver started while the run directory was locked"; fails=$((fails+1))
elif ! echo "$out" | grep -q "another census is already using"; then
  echo "FAIL: refused, but not because of the lock:"; echo "$out" | sed 's/^/      /' | head -4
  fails=$((fails+1))
else
  echo "ok: a locked run directory is refused"
  echo "$out" | grep -q "fuser -v" \
    && echo "ok: the refusal says how to find the holder" \
    || { echo "FAIL: the refusal gives no way to find the holding process"; fails=$((fails+1)); }
fi

kill "$HOLDER" 2>/dev/null; wait "$HOLDER" 2>/dev/null
# Once released, the same directory is usable again.
out="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --only aaa \
        --out "$T/run" 2>&1)"
echo "$out" | grep -q "lock held" \
  && echo "ok: once released, the directory is usable" \
  || { echo "FAIL: a released lock still blocked the run"; echo "$out" | sed 's/^/      /' | head -3; fails=$((fails+1)); }

# A default (no --out) run must get a unique, timestamped directory.
a="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 RUN_ID=idA bash "$DRV" --only aaa 2>&1 | grep -oP 'run_id=\K\S+')"
b="$(LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 RUN_ID=idB bash "$DRV" --only aaa 2>&1 | grep -oP 'run_id=\K\S+')"
[ -n "$a" ] && [ "$a" != "$b" ] \
  && echo "ok: each run gets its own run id ($a vs $b)" \
  || { echo "FAIL: runs did not get distinct run ids ('$a' vs '$b')"; fails=$((fails+1)); }

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: census_lock_test"
