#!/usr/bin/env bash
# check_fresh.sh <repository root> [<natives directory> <driver>...]: the build a test runs on was
# built from the current sources. Without a natives directory: formal/lean/.olean-dev, whose
# SOURCES.sha256 .build-proj.sh writes (every module it built). With one: each named driver's
# <driver>.sources.sha256 there, which build-opt.sh writes (every module linked into it, and its
# own source). Prints one FAIL line and the changed files, and exits 1, on a source that changed
# since the build, a file it lists that is gone, or no list at all; prints nothing and exits 0
# otherwise. The self-tests call it before their first driver run, so a stale build fails the
# test instead of passing on old code.
set -uo pipefail
R=$1; shift
check() { # <list> <what>
  local list=$1 what=$2 bad
  [[ -s "$list" ]] || { echo "FAIL: stale $what: $list is missing (rebuild: formal/lean/.build-proj.sh or build-opt.sh)"; return 1; }
  bad=$(cd "$R" && sha256sum --quiet -c "$list" 2>&1) \
    || { echo "FAIL: stale $what: sources changed since it was built (rebuild it):"; echo "$bad" | head -10; return 1; }
}
if [[ $# -eq 0 ]]; then
  check "$R/formal/lean/.olean-dev/SOURCES.sha256" ".olean-dev"
else
  NATD=$1; shift
  for d in "$@"; do check "$NATD/$d.sources.sha256" "native $d in $NATD" || exit 1; done
fi
