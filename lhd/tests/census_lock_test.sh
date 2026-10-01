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
# CENSUS_DRIVER lets this test run against a mutated copy, so the regression
# can be shown to FAIL against the previous in-tree-lock design.
DRV="${CENSUS_DRIVER:-}"
[ -n "$DRV" ] || for c in "${TEST_SRCDIR:-}/_main/scripts/coreet_d2_census.sh" \
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

# The lock lives OUTSIDE the run tree, keyed by the canonical output path --
# the first version put it at <out>/.lock, which does not survive the `rm -rf`
# that precedes a relaunch, so both jobs proceeded. Derive the same path the
# driver does.
ROOT_DIR="$(cd "$(dirname "$DRV")/.." && pwd)"
lock_for() { printf '%s' "$(readlink -m "$1")" | sha256sum | cut -d' ' -f1; }
LOCKF="$ROOT_DIR/generated/census_d2/runtime_locks/$(lock_for "$T/run").lock"
mkdir -p "$(dirname "$LOCKF")"

start_holder() {  # holds the stable lock, as a live run (or its orphan) would
  ( exec 9>"$LOCKF"; flock 9; sleep 30 ) &
  HOLDER=$!
  for _ in $(seq 1 60); do
    flock -n "$LOCKF" true 2>/dev/null || return 0
    sleep 0.1
  done
  echo "FAIL: the holder never acquired the lock"; exit 1
}
try_run() { LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --only aaa --out "$T/run" 2>&1; }

echo "--- a held lock refuses a second run ---"
start_holder
out="$(try_run)"; rc=$?
# Getting PAST the lock is the failure, whatever happens afterwards: a driver
# that prints a run id has already begun writing the tree.
if echo "$out" | grep -q "run_id="; then
  echo "FAIL: the driver got PAST the lock and started a run (rc=$rc)"
  echo "      -- it may have failed later for an unrelated reason, but by then"
  echo "         two jobs were already targeting the same tree"
  fails=$((fails+1))
elif ! echo "$out" | grep -q "another census is already using"; then
  echo "FAIL: did not start, but not because of the lock:"; echo "$out" | sed 's/^/      /' | head -4; fails=$((fails+1))
else
  echo "ok: a locked run directory is refused"
  echo "$out" | grep -q "fuser -v" && echo "ok: the refusal says how to find the holder" \
    || { echo "FAIL: the refusal gives no way to find the holder"; fails=$((fails+1)); }
  echo "$out" | grep -q "runtime_locks" \
    && echo "ok: the refusal names the stable lock path" \
    || { echo "FAIL: the refusal does not name a lock outside the run tree"; fails=$((fails+1)); }
fi

echo "--- THE ACTUAL INCIDENT: rm -rf the run tree, then relaunch ---"
# This is what the previous <out>/.lock could not catch: the holder keeps a
# lock on an UNLINKED inode and the relaunch makes a fresh one.
rm -rf "$T/run"; mkdir -p "$T/run"
out="$(try_run)"; rc=$?
if echo "$out" | grep -q "run_id="; then
  echo "FAIL: after rm -rf + recreate, a SECOND driver got past the lock (rc=$rc)"
  echo "      (this is EXACTLY the incident the lock exists to prevent: the holder"
  echo "       keeps a lock on an unlinked inode and the relaunch makes a new one)"
  fails=$((fails+1))
elif echo "$out" | grep -q "another census is already using"; then
  echo "ok: the lock survives rm -rf of the run tree and still refuses"
else
  echo "FAIL: refused after rm -rf, but not because of the lock:"; echo "$out" | sed 's/^/      /' | head -4
  fails=$((fails+1))
fi

kill "$HOLDER" 2>/dev/null; wait "$HOLDER" 2>/dev/null
out="$(try_run)"
echo "$out" | grep -q "lock: " \
  && echo "ok: once released, the directory is usable" \
  || { echo "FAIL: a released lock still blocked the run"; echo "$out" | sed 's/^/      /' | head -3; fails=$((fails+1)); }

echo "--- two TRULY default launches must not share a directory ---"
# No RUN_ID preset: asserting that two explicit strings differ proves nothing.
# Launched back to back so they land in the same wall-clock second.
d1="$T/d1.txt"; d2="$T/d2.txt"
( cd "$ROOT_DIR" && LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --only aaa >"$d1" 2>&1 ) &
p1=$!
( cd "$ROOT_DIR" && LIST="$T/corpus.txt" EXPECT_N=3 SKIP_SWEEP=1 bash "$DRV" --only aaa >"$d2" 2>&1 ) &
p2=$!
wait $p1 $p2 2>/dev/null
a="$(grep -oP 'run_id=\K\S+' "$d1" | head -1)"
b="$(grep -oP 'run_id=\K\S+' "$d2" | head -1)"
if [ -z "$a" ] || [ -z "$b" ]; then
  echo "FAIL: a default launch produced no run id (a='$a' b='$b')"; fails=$((fails+1))
elif [ "$a" = "$b" ]; then
  echo "FAIL: two concurrent default launches shared run id $a"; fails=$((fails+1))
else
  echo "ok: concurrent default launches get distinct run ids ($a, $b)"
fi
for r in "$a" "$b"; do [ -n "$r" ] && rm -rf "$ROOT_DIR/generated/census_d2/$r"; done

[ "$fails" -eq 0 ] || { echo "FAIL: $fails case(s) failed"; exit 1; }
echo "PASS: census_lock_test"
