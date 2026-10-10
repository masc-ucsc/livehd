#!/usr/bin/env bash
# Controls for the sweep driver's `lake` contract.
#
# Two defects were found by an out-of-tree probe and are pinned here:
#
#   1. An explicitly set LAKE that does not exist was SILENTLY REPLACED by one
#      found on PATH; the sweep then ran, wrote `reconciled=YES` and exited 0 --
#      measuring a different toolchain than the operator named.
#   2. `lake_version` took the first line of combined stdout/stderr, so a wrapper
#      printing a warning first put `warning: ...` into provenance as the
#      version.  A provenance field that lies is worse than one that is absent.
#
# Plus one positive control, so the checks cannot pass by refusing everything.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
SWEEP="$ROOT/scripts/run_d4_runtime_sweep.sh"
mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/lake_ctl.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

REAL_LAKE="${REAL_LAKE:-$HOME/.elan/bin/lake}"
[[ -x "$REAL_LAKE" ]] || REAL_LAKE="$(command -v lake 2>/dev/null || true)"
[[ -x "$REAL_LAKE" ]] || { echo "SKIP: no real lake available"; exit 0; }

# NOT `null_vpu`: that design is DEGENERATE (its certificate has no nodes), so
# the emitter's own static gates reject it and it has always exited 3.  It only
# looked like a positive control while the driver ignored a nonzero emitter rc.
# These controls are about the LAKE contract, which is corpus-independent, so
# the emission is a named helper writing a real certificate.
printf 'selftest_lake\n' > "$TMP/one.list"
LAKE_TINY="$TMP/tiny.dcert"
( cd "$ROOT/formal/lean" && "$REAL_LAKE" env lean --run probes/d4_write_tiny_cert.lean "$LAKE_TINY" ) >/dev/null 2>&1
cat > "$TMP/emit_ok.sh" <<EOF
#!/usr/bin/env bash
mkdir -p "\$1/lean" && cp "$LAKE_TINY" "\$1/lean/\$2.dcert"
EOF
chmod +x "$TMP/emit_ok.sh"
[[ -s "$LAKE_TINY" ]] || { echo "SKIP: could not build a certificate for the positive control"; exit 0; }
fails=0
ok()   { printf 'PASS  %s\n' "$1"; }
bad()  { printf 'FAIL  %s\n' "$1"; fails=$((fails + 1)); }

# --- 1. explicit invalid LAKE, with a VALID lake on PATH -------------------
# The fallback must not engage, no output directory may be populated, and the
# exit status must be non-zero.
out="$TMP/ctl_explicit"
PATH="$(dirname "$REAL_LAKE"):$PATH" LAKE=/nonexistent/lake \
  SELFTEST_EMIT_CMD="$TMP/emit_ok.sh" "$SWEEP" selftest "$TMP/one.list" "$out" >"$TMP/explicit.log" 2>&1
rc=$?
if [[ "$rc" -eq 0 ]]; then
  bad "explicit invalid LAKE exited 0"
elif ! grep -q 'explicit setting is never replaced' "$TMP/explicit.log"; then
  bad "explicit invalid LAKE failed for the wrong reason: $(head -1 "$TMP/explicit.log")"
elif [[ -s "$out/rows.tsv" ]]; then
  bad "explicit invalid LAKE still produced rows"
elif grep -qs 'reconciled=YES' "$out/provenance.txt"; then
  bad "explicit invalid LAKE still claimed reconciled coverage"
else
  ok "explicit invalid LAKE fails closed, no rows, no reconciled coverage"
fi

# --- 2. a warning before the version line ----------------------------------
cat > "$TMP/lake_noisy" <<EOF
#!/usr/bin/env bash
if [[ "\${1:-}" == "--version" ]]; then
  echo "warning: reviewer injected version prelude" >&2
  exec "$REAL_LAKE" --version
fi
exec "$REAL_LAKE" "\$@"
EOF
chmod +x "$TMP/lake_noisy"
out2="$TMP/ctl_noisy"
LAKE="$TMP/lake_noisy" SELFTEST_EMIT_CMD="$TMP/emit_ok.sh" "$SWEEP" selftest "$TMP/one.list" "$out2" >"$TMP/noisy.log" 2>&1
rc=$?
ver="$(grep -m1 '^lake_version=' "$out2/provenance.txt" 2>/dev/null | cut -d= -f2-)"
if [[ "$rc" -ne 0 ]]; then
  bad "warning-before-version run failed: $(tail -1 "$TMP/noisy.log")"
elif [[ "$ver" != "Lake version "* ]]; then
  bad "lake_version recorded a non-version line: '$ver'"
elif ! grep -q '^lake_version_prelude=' "$out2/provenance.txt"; then
  bad "the warning was dropped instead of recorded separately"
else
  ok "warning before the version line: lake_version='$ver', prelude kept separately"
fi

# --- 3. positive control ---------------------------------------------------
if [[ "$rc" -eq 0 ]] && [[ "$(tail -n +2 "$out2/rows.tsv" 2>/dev/null | wc -l)" == "1" ]] \
   && [[ "$(tail -n +2 "$out2/rows.tsv" | awk -F'\t' '{print NF}')" == "18" ]] \
   && [[ "$(tail -n +2 "$out2/rows.tsv" | cut -f13)" == "RAN" ]]; then
  ok "the positive control produced one 18-field row with runtime_status=RAN"
else
  bad "positive control did not produce a single RAN row: $(tail -n +2 "$out2/rows.tsv" 2>/dev/null | cut -f3,13,16 | head -1)"
fi

[[ "$fails" -eq 0 ]] && { echo "ALL CONTROLS PASS"; exit 0; }
echo "$fails CONTROL(S) FAILED"; exit 1
