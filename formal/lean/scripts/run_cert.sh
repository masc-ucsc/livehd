#!/usr/bin/env bash
# Run one DCERT1 certificate through BOTH paths and report.
#
#   run_cert.sh --emit <dir>            write the fixtures out as .dcert
#   run_cert.sh <file.dcert> [cycles] [seed]
#
# Peak RSS is reported twice on purpose: the runner reads its own VmHWM, and
# this wrapper asks the kernel through /usr/bin/time -- they should agree.
set -u
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export LEAN_PATH="$HERE/../.olean-dev"
exec /usr/bin/time -f "wrapper: %e s wall, %M KB peak RSS" \
  nice -n19 ionice -c3 lean --run "$HERE/cert_runner.lean" "$@"
