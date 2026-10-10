#!/usr/bin/env bash
# Controls for `scripts/replay_d4_certs.sh`'s FINAL GATE.
#
# The defect these pin: the script counted `not_ran` and then left it out of the
# exit predicate.  A MALFORMED certificate carrying its correct recorded SHA-256
# produced one well-shaped `PARSE_ERROR` / `NOT_RUN` row, reconciled cleanly,
# reported `ran=0`, and STILL EXITED 0 -- a replay that measured nothing and
# called it success.
#
# The four reasons a replay can fail must stay distinguishable, so each control
# asserts not only "non-zero" but WHY:
#
#   1. positive            a good certificate replays, exit 0
#   2. malformed, SHA OK   exit non-zero for the RUNTIME failure
#                          (cert_rejected=0: not a missing file, not a bad hash)
#   3. certificate gone    exit non-zero as CERT_MISSING
#   4. recorded SHA wrong  exit non-zero as CERT_SHA_MISMATCH
#   5. digest moved        exit non-zero as CHANGED
#
# The certificate is MADE here by `probes/d4_write_tiny_cert.lean`, not borrowed
# from a sweep's `temp/`, so this test does not depend on untracked leftovers.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
REPLAY="$ROOT/scripts/replay_d4_certs.sh"
LEANDIR="$ROOT/formal/lean"

LAKE="${LAKE:-$HOME/.elan/bin/lake}"
[[ -x "$LAKE" ]] || LAKE="$(command -v lake 2>/dev/null || true)"
[[ -x "$LAKE" ]] || { echo "SKIP: no lake available"; exit 0; }
[[ -x "$REPLAY" ]] || { echo "FATAL: missing $REPLAY" >&2; exit 2; }

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/replay_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

fails=0
ok()  { printf 'PASS  %s\n' "$1"; }
bad() { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

HEADER=$'corpus\tmodule\temit_status\tcert_path\tcert_sha256\tsources\tnodes\tflops\tmemories\tparse_status\tparse_ms\tcompiles_ok\truntime_status\trun_ms\tdigest\tdiagnostic\twall_s\tpeak_rss_kb'

CERT="$TMP/tiny.dcert"
( cd "$LEANDIR" && "$LAKE" env lean --run probes/d4_write_tiny_cert.lean "$CERT" ) >/dev/null 2>&1
[[ -s "$CERT" ]] || { echo "FATAL: could not write the tiny certificate" >&2; exit 2; }

# The certificate's REAL digest, taken from the row driver itself.
REALROW="$( cd "$LEANDIR" && "$LAKE" env lean --run probes/d4_sweep_row.lean "$CERT" 2>/dev/null | tail -1 )"
REALDIG="$(cut -f10 <<<"$REALROW")"
[[ -n "$REALDIG" && "$(cut -f8 <<<"$REALROW")" == "RAN" ]] || {
  echo "FATAL: the tiny certificate does not run; controls would be meaningless" >&2; exit 2; }

# write_recorded <file> <cert-path> <sha> <digest>
write_recorded() {
  printf '%s\n' "$HEADER" > "$1"
  printf 'coreet\ttiny\tEMITTED\t%s\t%s\t4\t2\t1\t0\tPARSED\t1\ttrue\tRAN\t0\t%s\t-\t0.1\t1000\n' \
    "$2" "$3" "$4" >> "$1"
}
field() { grep -m1 "^$2=" "$1/provenance.txt" 2>/dev/null | cut -d= -f2-; }
verdict() { tail -n +2 "$1/digest_compare.tsv" 2>/dev/null | head -1 | cut -f4; }

run_replay() {  # run_replay <recorded> <outdir>; echoes exit status
  ( cd "$ROOT" && "$REPLAY" "$1" "$2" ) >"$2.log" 2>&1
  echo $?
}

GOODSHA="$(sha256sum "$CERT" | cut -d' ' -f1)"

# --- 1. positive control ----------------------------------------------------
write_recorded "$TMP/rec_good.tsv" "$CERT" "$GOODSHA" "$REALDIG"
rc="$(run_replay "$TMP/rec_good.tsv" "$TMP/out_good")"
if [[ "$rc" -ne 0 ]]; then
  bad "positive control did not exit 0: $(tail -1 "$TMP/out_good.log")"
elif [[ "$(field "$TMP/out_good" runtime_complete)" != "YES" ]]; then
  bad "positive control did not record runtime_complete=YES"
elif [[ "$(field "$TMP/out_good" digest_preserved)" != "1" ]]; then
  bad "positive control did not preserve the digest"
else
  ok "positive control: a good certificate replays, exit 0, runtime_complete=YES"
fi

# --- 2. malformed certificate, CORRECT recorded SHA -------------------------
# The reviewer's case.  Shape reconciles; the run did not happen.
BADCERT="$TMP/malformed.dcert"
sed 's/^DCERT1$/DCERT1/; 2s/^4$/banana/' "$CERT" > "$BADCERT"
cmp -s "$CERT" "$BADCERT" && { echo "FATAL: corruption was a no-op" >&2; exit 2; }
BADSHA="$(sha256sum "$BADCERT" | cut -d' ' -f1)"
write_recorded "$TMP/rec_bad.tsv" "$BADCERT" "$BADSHA" "$REALDIG"
rc="$(run_replay "$TMP/rec_bad.tsv" "$TMP/out_bad")"
if [[ "$rc" -eq 0 ]]; then
  bad "a malformed certificate with a matching SHA still exited 0"
elif [[ "$(field "$TMP/out_bad" cert_rejected)" != "0" ]]; then
  bad "malformed control failed as a REJECTED certificate, not as a runtime failure"
elif [[ "$(field "$TMP/out_bad" reconciled)" != "YES" ]]; then
  bad "malformed control failed on SHAPE; the row contract must still reconcile"
elif [[ "$(field "$TMP/out_bad" runtime_complete)" != "NO" ]]; then
  bad "malformed control did not record runtime_complete=NO"
elif [[ "$(field "$TMP/out_bad" not_ran)" != "1" || "$(field "$TMP/out_bad" ran)" != "0" ]]; then
  bad "malformed control miscounted: ran=$(field "$TMP/out_bad" ran) not_ran=$(field "$TMP/out_bad" not_ran)"
elif [[ "$(awk -F'\t' 'NR==2{print $10}' "$TMP/out_bad/rows.tsv")" != "PARSE_ERROR" ]]; then
  bad "malformed control did not report PARSE_ERROR: $(awk -F'\t' 'NR==2{print $10}' "$TMP/out_bad/rows.tsv")"
else
  ok "malformed certificate, matching SHA: exit $rc for the RUNTIME failure (reconciled=YES, runtime_complete=NO)"
fi

# --- 3. certificate gone ----------------------------------------------------
write_recorded "$TMP/rec_gone.tsv" "$TMP/does_not_exist.dcert" "$GOODSHA" "$REALDIG"
rc="$(run_replay "$TMP/rec_gone.tsv" "$TMP/out_gone")"
if [[ "$rc" -eq 0 ]]; then
  bad "a missing certificate still exited 0"
elif [[ "$(verdict "$TMP/out_gone")" != "CERT_MISSING" ]]; then
  bad "missing certificate reported '$(verdict "$TMP/out_gone")', not CERT_MISSING"
else
  ok "missing certificate: exit $rc, reported as CERT_MISSING"
fi

# --- 4. recorded SHA does not match the file --------------------------------
write_recorded "$TMP/rec_sha.tsv" "$CERT" "${GOODSHA//[0-9]/0}" "$REALDIG"
rc="$(run_replay "$TMP/rec_sha.tsv" "$TMP/out_sha")"
if [[ "$rc" -eq 0 ]]; then
  bad "a recorded SHA mismatch still exited 0"
elif [[ "$(verdict "$TMP/out_sha")" != "CERT_SHA_MISMATCH" ]]; then
  bad "SHA mismatch reported '$(verdict "$TMP/out_sha")', not CERT_SHA_MISMATCH"
elif [[ "$(field "$TMP/out_sha" ran)" != "0" ]]; then
  bad "SHA mismatch ran the certificate anyway"
else
  ok "recorded SHA mismatch: exit $rc, refused as CERT_SHA_MISMATCH, never run"
fi

# --- 5. the digest moved ----------------------------------------------------
write_recorded "$TMP/rec_dig.tsv" "$CERT" "$GOODSHA" "1234567890123456789"
rc="$(run_replay "$TMP/rec_dig.tsv" "$TMP/out_dig")"
if [[ "$rc" -eq 0 ]]; then
  bad "a changed digest still exited 0"
elif [[ "$(verdict "$TMP/out_dig")" != "CHANGED" ]]; then
  bad "changed digest reported '$(verdict "$TMP/out_dig")', not CHANGED"
else
  ok "changed digest: exit $rc, reported as CHANGED"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
