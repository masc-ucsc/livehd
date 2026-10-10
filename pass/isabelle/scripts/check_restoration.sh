#!/usr/bin/env bash
# Check the recovered library, synthetic combiner, and freshly emitted models.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"
OUT="${OUT:-$ROOT/generated/isabelle_restoration}"
TEST_BIN="${TEST_BIN:-$ROOT/bazel-bin/pass/isabelle/emission_smoke}"
ISABELLE="${ISABELLE:-isabelle}"
mkdir -p "$OUT"
OUT="$(cd "$OUT" && pwd)"
validation_user_root="${ISABELLE_VALIDATION_USER_ROOT:-$OUT/isabelle_home}"
mkdir -p "$validation_user_root" "$OUT/runtime_tmp"
# USER_HOME is Isabelle's supported user-directory override; leave shell HOME alone.
export TMPDIR="$OUT/runtime_tmp" TMP="$OUT/runtime_tmp" TEMP="$OUT/runtime_tmp"
settings_dir="$(env USER_HOME="$validation_user_root" "$ISABELLE" getenv -b ISABELLE_HOME_USER)/etc"
mkdir -p "$settings_dir"
printf 'ISABELLE_TMP_PREFIX="%s/isabelle"\n' "$OUT/runtime_tmp" > "$settings_dir/settings"
printf 'ISABELLE_JAVA_SYSTEM_OPTIONS="$ISABELLE_JAVA_SYSTEM_OPTIONS -Djava.io.tmpdir=%s"\n' "$OUT/runtime_tmp" >> "$settings_dir/settings"

# A new directory ensures a missing test cannot reuse old generated theories.
cases="$(mktemp -d "$OUT/cases.XXXXXXXXXX")"
ISABELLE_EMISSION_FIXTURES="$cases" "$TEST_BIN" > "$OUT/generate.log" 2>&1
python3 "$ROOT/pass/isabelle/scripts/bakeoff_gen.py" eqns 32 > "$cases/Bake.thy"
cat > "$cases/ROOT" <<'ROOT_EOF'
session IsabelleRestoration = "LGraph-Translation-Correctness" +
  options [document = false, browser_info = false, timeout = 300]
  theories
    Bake
    restore_shift_Lgraph_Cert
    RestoreShiftOracle
    restore_comb_Oracle
    restore_seq_Oracle
    restore_memory_Lgraph_Cert
ROOT_EOF
printf '%s\n' "$cases" > "$OUT/fixtures.txt"
env USER_HOME="$validation_user_root" /usr/bin/time -v -o "$OUT/proofs.time" \
  "$ISABELLE" build -j 1 -o threads=8 \
  -d "$ROOT/formal/semantic_primitives" -d "$ROOT/formal/translation_correctness" \
  -D "$cases" > "$OUT/proofs.log" 2>&1
if grep -Eq 'sorry|cheating' "$OUT/proofs.log"; then
  echo "FAIL: proof validation reported a placeholder; see $OUT/proofs.log" >&2
  exit 1
fi
echo "PASS: restored Isabelle library, synthetic bridge, emitted models and value checks"
echo "Measurements: $OUT/proofs.time"
