#!/usr/bin/env bash
# Controls for the D4 certificate store (decision #15).
#
# The runtime sweep (scripts/run_d4_runtime_sweep.sh) keeps every certificate
# it emits as <CERT_STORE>/<sha256>.dcert, outside git; a result row's
# `cert_sha256` is the key. The replay (scripts/replay_d4_certs.sh) finds a
# certificate by that sha256 in the store when the row's recorded path is gone.
# Before this, 0 of 524 swept certificates were on disk, so 19 changed digests
# could not be reproduced.
#
# On two small CORE-ET modules:
#   1. the sweep fills the store: one file per emitted certificate, named by
#      the sha256 of its own bytes;
#   2. with the sweep's work directories DELETED, a replay with CERT_STORE
#      runs every certificate and preserves every recorded digest;
#   3. control -- the same replay without CERT_STORE cannot run them;
#   4. control -- a store entry whose bytes no longer hash to its name is
#      refused (CERT_SHA_MISMATCH), never run.
#
# Usage: cert_store_controls.sh [workdir]
# Env: LHD (an -c opt lhd), COREET_ROOT, LAKE -- as run_d4_runtime_sweep.sh.
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../../.." && pwd)"
W="${1:-$ROOT/generated/d4_cert_store_controls}"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }
rm -rf "$W"
mkdir -p "$W"
W="$(cd "$W" && pwd)"
fail=0
ok()  { printf 'ok: %s\n' "$1"; }
bad() { printf 'CONTROL FAILED: %s\n' "$1"; fail=$((fail + 1)); }

printf 'tima_adder\ntrans_rsqrt_rom_case_c0\n' > "$W/list.txt"
env -u TMPDIR CERT_STORE="$W/store" bash "$ROOT/scripts/run_d4_runtime_sweep.sh" coreet "$W/list.txt" "$W/sweep" 2 \
  > "$W/sweep.log" 2>&1 || die "the sweep failed; see $W/sweep.log"
ROWS="$W/sweep/rows.tsv"
[[ -s "$ROWS" ]] || die "the sweep wrote no rows"
grep -q "^cert_store=$W/store$" "$W/sweep/provenance.txt" || bad "the sweep's provenance does not name the store"

# 1. one store file per emitted certificate, named by its own sha256
n_rows=$(awk -F'\t' 'NR>1 && $5 != "-" && $5 != ""' "$ROWS" | wc -l)
n_store=$(ls "$W/store" | wc -l)
named=0
for f in "$W"/store/*.dcert; do
  [[ "$(sha256sum "$f" | cut -d' ' -f1).dcert" == "$(basename "$f")" ]] && named=$((named + 1))
done
if [[ "$n_rows" -eq 2 && "$n_store" -eq 2 && "$named" -eq 2 ]]; then
  ok "the sweep kept both certificates in the store, each named by the sha256 of its bytes"
else
  bad "store: $n_rows rows with a certificate, $n_store files, $named named by their sha256"
fi

# 2. the work directories gone, the replay runs from the store
rm -rf "$W/sweep/blocks"
env -u TMPDIR CERT_STORE="$W/store" bash "$ROOT/scripts/replay_d4_certs.sh" "$ROWS" "$W/replay" > "$W/replay.log" 2>&1
pres=$(awk -F'\t' '$4 == "PRESERVED"' "$W/replay/digest_compare.tsv" 2>/dev/null | wc -l)
if [[ "$pres" -eq 2 ]]; then
  ok "with the work directories deleted, the replay found both certificates by sha256 and preserved both digests"
else
  bad "replay from the store preserved $pres of 2 digests (see $W/replay.log)"
fi

# 3. control: no store, nothing to run
env -u TMPDIR -u CERT_STORE bash "$ROOT/scripts/replay_d4_certs.sh" "$ROWS" "$W/replay_nostore" > "$W/replay_nostore.log" 2>&1
miss=$(awk -F'\t' '$4 == "CERT_MISSING"' "$W/replay_nostore/digest_compare.tsv" 2>/dev/null | wc -l)
if [[ "$miss" -eq 2 ]]; then
  ok "control: without CERT_STORE the same replay reports both certificates missing"
else
  bad "without CERT_STORE, $miss of 2 rows were reported missing"
fi

# 4. control: a store entry that no longer hashes to its name is refused
victim=$(ls "$W"/store/*.dcert | head -1)
printf '\n' >> "$victim"
env -u TMPDIR CERT_STORE="$W/store" bash "$ROOT/scripts/replay_d4_certs.sh" "$ROWS" "$W/replay_bad" > "$W/replay_bad.log" 2>&1
mism=$(awk -F'\t' '$4 == "CERT_SHA_MISMATCH"' "$W/replay_bad/digest_compare.tsv" 2>/dev/null | wc -l)
if [[ "$mism" -eq 1 ]]; then
  ok "control: a store entry changed after the sweep is refused (CERT_SHA_MISMATCH), not run"
else
  bad "a changed store entry gave $mism CERT_SHA_MISMATCH rows"
fi

if [[ "$fail" -eq 0 ]]; then
  echo "PASS: cert_store_controls"
  exit 0
fi
echo "FAIL: $fail control(s)"
exit 1
