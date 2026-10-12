#!/usr/bin/env bash
# TRANSLATION-VALIDATION coverage sweep, run from a PRIVATE clean checkout.
#
# PROVENANCE OF THIS FILE, stated plainly: it is a RETAINED RECONSTRUCTION of the
# commands that produced generated/master_port/cov12x5/tv12x5/run/, written after
# that run had already been launched by hand.  As of this writing it has not
# itself been invoked, and nothing in the retained evidence was produced BY it.
# The argv, environment and gates it issues are the ones that run used -- that is
# what makes it a reconstruction rather than a sketch -- but any claim of the
# form "this script produced that result" would be false and is not made.
#
# WHY A systemd USER SCOPE.  `d3_sweep.py --enforce cgroup` refuses outright when
# cgroup v2 memory delegation is unusable, which is correct: running under the
# sampled advisory limit while claiming kernel enforcement would be a false
# guarantee.  From an ordinary login shell it IS unusable here -- the session
# sits in `session-N.scope`, whose `cgroup.subtree_control` is empty, and
# migrating into the `user@.service` subtree crosses a root-owned common
# ancestor.  Launching inside `systemd-run --user --scope` puts the driver under
# `user@.service/app.slice/`, where the delegated preflight passes.
#
# WHAT IS DELIBERATELY NOT PASSED:
#   --allow-dirty          a result nobody can regenerate is not a measurement
#   --runner-selftest      brands the artifact digest as non-canonical
#   D3_ARTIFACT_DIR        changes what is HASHED without changing what is LOADED
#   --prove                a separate obligation; the proof gate reads `na` here
#
#   tv_cov_sweep.sh <private-checkout> <manifest.tsv> <certs-dir> <out-dir> [timeout_s] [kill_rss_kib]
set -uo pipefail
P=${1:?private checkout root}; MAN=${2:?manifest}; CERTS=${3:?certs dir}; OUT=${4:?out dir}
TMO=${5:-1800}; KILL=${6:-16000000}       # KILL is KiB, as --kill-over-rss-kb expects

case "$TMO"  in ''|*[!0-9]*) echo "REFUSING: timeout $TMO is not a number"; exit 2 ;; esac
case "$KILL" in ''|*[!0-9]*) echo "REFUSING: kill_rss_kib $KILL is not a number"; exit 2 ;; esac
[ "$TMO"  -gt 0 ] || { echo "REFUSING: timeout must be positive"; exit 2; }
[ "$KILL" -gt 0 ] || { echo "REFUSING: --enforce cgroup needs a POSITIVE kill_rss_kib"; exit 2; }

D3=$P/pass/lean/scripts/d3_sweep.py
[ -f "$D3" ] || { echo "REFUSING: no d3_sweep.py at $D3"; exit 2; }
[ -d "$CERTS" ] || { echo "REFUSING: certs dir $CERTS does not exist"; exit 2; }
[ -f "$MAN" ] || { echo "REFUSING: manifest $MAN does not exist"; exit 2; }

# The build root must be the private one, and it must be clean: a dirty
# worktree, a symlinked .lake or a shared root all make the row unreproducible,
# and d3_sweep refuses each of them on its own.  Checked here too so the refusal
# is legible before a 20-minute probe starts.
[ -L "$P/formal/lean/.lake" ] && { echo "REFUSING: $P/formal/lean/.lake is a symlink"; exit 2; }
if [ -n "$(git -C "$P" status --porcelain)" ]; then
  echo "REFUSING: private checkout $P is dirty; d3_sweep would refuse the manifest run"; exit 2
fi

# ATOMIC attempt root.  `mkdir -p` on an existing directory would overwrite that
# attempt's BINDING.txt, logs and sweep.tsv -- destroying the evidence of a
# previous run while looking like a fresh one.  A plain mkdir refuses instead.
mkdir "$OUT" 2>/dev/null || { echo "REFUSING: attempt root $OUT already exists; \
pick a new one rather than overwriting a retained attempt"; exit 3; }

{ echo "launched_at=$(date -Iseconds)"
  echo "host=$(uname -n) kernel=$(uname -r)"
  echo "private_root=$P"
  echo "private_head=$(git -C "$P" rev-parse HEAD)"
  echo "private_status_clean=yes"
  echo "D3_LEAN_ROOT=$P/formal/lean"
  echo "driver=$D3 sha256=$(sha256sum "$D3" | cut -d' ' -f1)"
  echo "manifest=$MAN sha256=$(sha256sum "$MAN" | cut -d' ' -f1)"
  echo "certs_dir=$CERTS"
  for f in "$CERTS"/*; do
    [ -f "$f" ] || continue
    echo "cert=$(basename "$f") sha256=$(sha256sum "$f" | cut -d' ' -f1) bytes=$(stat -c%s "$f")"
  done
  echo "lake=$(cd "$P/formal/lean" && lake --version 2>&1 | head -1)"
  echo "lean_toolchain=$(cat "$P/formal/lean/lean-toolchain" 2>/dev/null)"
  echo "reifier=legacy (default) prove=NOT REQUESTED -- generation/admission sweep only"
  echo "enforce=cgroup kill_over_rss_kb=$KILL (KiB) jobs=1 timeout=${TMO}s"
} > "$OUT/BINDING.txt"

cd "$P" || exit 2
systemd-run --user --scope --quiet --unit="tvsweep-$(date +%s)" \
  /usr/bin/time -f 'wall %e\nuser %U\nsys %S\nmaxrss_kib %M\nexit %x' -o "$OUT/time.txt" \
  nice -n19 ionice -c3 env D3_LEAN_ROOT="$P/formal/lean" LEAN_NUM_THREADS=4 \
  python3 -I "$D3" \
    --manifest "$MAN" --certs "$CERTS" --out "$OUT/sweep.tsv" \
    --jobs 1 --timeout "$TMO" --enforce cgroup --kill-over-rss-kb "$KILL" \
  > "$OUT/stdout.log" 2> "$OUT/stderr.log"
rc=$?
echo "$rc" > "$OUT/exit"

# BOTH records are required, as the prose says.  A wrapper rc of 0 with no
# parseable producer status is NOT a pass: it means the measurement was lost,
# and a lost measurement is reported as MISSING rather than credited.
t=$(awk '$1=="exit"{print $2; found=1} END{exit !found}' "$OUT/time.txt" 2>/dev/null)
tr=$?
if [ "$tr" != 0 ] || [ -z "$t" ]; then
  echo "MISSING exit=$rc time_exit=- (no parseable GNU time producer status)" | tee "$OUT/DISPOSITION.txt"
  exit 97
fi
if [ "$t" != "$rc" ]; then
  echo "DISAGREE exit=$rc time_exit=$t (wrapper and producer differ)" | tee "$OUT/DISPOSITION.txt"
  exit 96
fi
case "$rc" in
  0)   echo "OK exit=0 time_exit=$t" ;;
  124) echo "CAP exit=124 (timeout ${TMO}s; partial and UNDECIDED, neither a pass nor a capability refusal)" ;;
  *)   echo "REFUSED exit=$rc time_exit=$t" ;;
esac | tee "$OUT/DISPOSITION.txt"
# Propagate the TRUE producer exit.  A trailing echo must not turn a refusal or
# a timeout into a 0 for whatever launched this.
exit "$rc"
