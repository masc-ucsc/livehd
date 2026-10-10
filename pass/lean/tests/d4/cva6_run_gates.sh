#!/usr/bin/env bash
# The same three gates as `coreet_run_gates.sh`, over a CVA6 sweep.
#
# WHY A SECOND SCRIPT RATHER THAN A FLAG.  The CORE-ET and CVA6 drivers retain
# DIFFERENT artifacts, and papering over that would have meant renaming a
# directory to satisfy a checker:
#
#            CORE-ET                        CVA6
#   graph    blocks/<m>/lgdb_norm           blocks/<m>/lgdb   (the driver sets
#                                           formal.lean.normalize=true, so this
#                                           IS the normalized graph)
#   top      <m>                            <m>_gate          (the wrapper is
#                                           what gets elaborated)
#   lean     lean/<m>_Lgraph.lean           lean/<m>_gate_Lgraph.lean
#   cert     lean/<m>.dcert                 lean/<m>_gate.dcert
#
# The gates themselves are identical and are spelled out the same way the
# accepted canonical-30 evidence spells them:
#
#   vc_gates.py <lean> <top>                                   (static shape)
#   cert_lgraph_diff.py <lean> --quiet                         (invariant half)
#   cert_lgraph_diff.py <lean> --verilog <v> --require-verilog (REAL differential)
#
# with <v> regenerated HERE by `lhd compile lg:<that row's retained lgdb>
# --recipe O0` rather than taken on trust, and the row's own `cert_sha256`
# recomputed from the retained bytes.  A block counts only when its row says
# PARSED / true / RAN *and* its hash matches *and* all three gates pass.
#
# Usage: cva6_run_gates.sh <run-dir> <out.tsv> [keep-dir]
set -uo pipefail
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
# NO DEFAULT.  `bazel-bin/lhd/lhd` is a convenience symlink that follows the
# last-built configuration in an output base shared with concurrent sessions;
# three different hashes came out of one tracked-clean tree while the dG unit
# ran.  A gate table whose binary cannot be named is not evidence, so this
# refuses rather than reaching for that path.
LHD="${LHD:-}"
GATE_DIFF="$ROOT/pass/lean/scripts/cert_lgraph_diff.py"
GATE_VC="$ROOT/pass/lean/scripts/vc_gates.py"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }

RUN="${1:-}"; OUT="${2:-}"; KEEP="${3:-}"
[[ -n "$RUN" && -n "$OUT" ]] || die "usage: $0 <run-dir> <out.tsv> [keep-dir]"
[[ -s "$RUN/rows.tsv" ]] || die "no rows.tsv under $RUN"
[[ -n "$LHD" ]] || die "LHD is required and has no default.
       Resolve one for an EXPLICIT configuration first:
         pass/lean/tests/d4/resolve_lhd_artifact.sh <dest> -c dbg --repo_env=...
       and pass LHD=<dest>.  bazel-bin/lhd/lhd is a mutable symlink, not an identity."
[[ -x "$LHD" ]] || die "no lhd at $LHD"
LHD_REAL="$(readlink -f "$LHD")"
[[ "$LHD_REAL" != "$(readlink -f "$ROOT/bazel-bin/lhd/lhd" 2>/dev/null)" ]] \
  || die "LHD resolves to the bazel-bin convenience symlink's target.
       That path follows the last-built configuration in a shared output base.
       Use resolve_lhd_artifact.sh and pass the resolved COPY."
LHD_SHA="$(sha256sum < "$LHD" | cut -d' ' -f1)"
GATE_SHA="$( { cat "$GATE_VC" "$GATE_DIFF"; } | sha256sum | cut -d' ' -f1)"
RUN_ABS="$(cd "$RUN" && pwd)" || die "cannot resolve $RUN"
for g in "$GATE_DIFF" "$GATE_VC"; do [[ -r "$g" ]] || die "missing gate tool $g"; done
[[ -z "$KEEP" ]] || mkdir -p "$KEEP" || die "cannot create keep dir $KEEP"

TMP="$(mktemp -d "${TMPDIR:-/tmp}/cva6gate.XXXXXX")" || die "cannot create scratch"
trap 'rm -rf "$TMP"' EXIT
tmp="$TMP/table.tsv"

{
  echo "# Static + differential gates over a D4 CVA6 runtime sweep."
  echo "# cert_differential uses cgen Verilog regenerated from each block's own lgdb."
  echo "# cert_hash_matches_row recomputes the row's own cert_sha256 from the retained bytes."
  echo "# schema          = d4-cva6-gates/1"
  echo "# cert_sha256 and verilog_sha256 are FULL; lean_sha256 is the first 16 hex."
  echo "# run             = $RUN_ABS"
  echo "# lhd_sha256      = $LHD_SHA"
  echo "# gate_tool_sha256 = $GATE_SHA"
  echo "# gated_at        = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'module\temit_status\truntime_status\tvc_gates\tcert_invariant\tcert_differential\tcert_sha256\tverilog_sha256\tlean_sha256\tcert_hash_matches_row\tfirst_failure\n'
} > "$tmp"

cand=0; accepted=0; gatefail=0; hashfail=0
while IFS=$'\t' read -r corpus m emit cert_path cert_sha f6 f7 f8 f9 parse f11 compiles runst rest; do
  [[ "$corpus" == "corpus" || "$corpus" == \#* || -z "$m" ]] && continue
  if [[ "$emit" != "EMITTED" || "$parse" != "PARSED" || "$compiles" != "true" || "$runst" != "RAN" ]]; then
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\tnot a candidate RAN row\n' "$m" "$emit" "${runst:--}" >> "$tmp"
    continue
  fi
  cand=$((cand + 1))
  top="${m}_gate"
  lean="$RUN/blocks/$m/lean/${top}_Lgraph.lean"
  cert="$RUN/blocks/$m/lean/${top}.dcert"
  norm="$RUN/blocks/$m/lgdb"
  if [[ ! -s "$lean" || ! -s "$cert" || ! -d "$norm" ]]; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\t-\t-\tretained artifact or lgdb missing\n' "$m" "$emit" "$runst" >> "$tmp"
    continue
  fi
  full="$(sha256sum "$cert" | cut -d' ' -f1)"
  if [[ "$full" == "$cert_sha" ]]; then match=yes; else match=no; hashfail=$((hashfail + 1)); fi

  rm -rf "$TMP/v" "$TMP/w"
  if ! "$LHD" compile "lg:$norm" --top "$top" --recipe O0 --emit-dir "verilog:$TMP/v" \
        --workdir "$TMP/w" -q >"$TMP/cgen.log" 2>&1; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t%s\t-\t%s\t%s\tcgen from lgdb failed\n' \
      "$m" "$emit" "$runst" "$full" "$(sha256sum "$lean" | cut -c1-16)" "$match" >> "$tmp"
    continue
  fi
  cat "$TMP"/v/*.v > "$TMP/$top.v" 2>/dev/null
  if [[ ! -s "$TMP/$top.v" ]]; then
    gatefail=$((gatefail + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t%s\t-\t%s\t%s\tcgen produced no verilog\n' \
      "$m" "$emit" "$runst" "$full" "$(sha256sum "$lean" | cut -c1-16)" "$match" >> "$tmp"
    continue
  fi
  [[ -z "$KEEP" ]] || cp -f "$TMP/$top.v" "$KEEP/$top.v"

  python3 -B "$GATE_VC" "$lean" "$top" --quiet >/dev/null 2>&1 && vc=PASS || vc=FAIL
  python3 -B "$GATE_DIFF" "$lean" --quiet >/dev/null 2>&1 && inv=PASS || inv=FAIL
  dout="$(python3 -B "$GATE_DIFF" "$lean" --verilog "$TMP/$top.v" --require-verilog 2>&1)"
  if [[ $? -eq 0 ]]; then diff=PASS; first='-'
  else diff=FAIL; first="$(grep -m1 '^FAIL' <<<"$dout")"; [[ -z "$first" ]] && first="$(tail -n1 <<<"$dout")"; fi

  if [[ "$vc" == PASS && "$inv" == PASS && "$diff" == PASS && "$match" == yes ]]; then
    accepted=$((accepted + 1))
  elif [[ "$match" == yes ]]; then
    gatefail=$((gatefail + 1))
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$emit" "$runst" "$vc" "$inv" "$diff" \
    "$full" "$(sha256sum "$TMP/$top.v" | cut -d' ' -f1)" "$(sha256sum "$lean" | cut -c1-16)" "$match" "$first" >> "$tmp"
done < "$RUN/rows.tsv"

declared="$(( $(wc -l < "$RUN/rows.tsv") - 1 ))"
{
  echo "#"
  echo "# declared rows                $declared"
  echo "# candidate RAN rows           $cand"
  echo "# ACCEPTED (row + hash + all three gates)   $accepted"
  echo "# gate failures                $gatefail"
  echo "# certificate hash mismatches  $hashfail"
} >> "$tmp"

cp -f "$tmp" "$OUT" || die "cannot write $OUT"
echo "wrote $OUT"
tail -6 "$OUT"
if [[ "$gatefail" -gt 0 || "$hashfail" -gt 0 || "$accepted" -ne "$cand" ]]; then
  grep -vE '^#|^module' "$OUT" | awk -F'\t' '$11 != "-" {printf "  %-32s %s\n", $1, $11}'
  echo "GATES FAILED: $accepted of $cand candidate rows accepted"
  exit 1
fi
echo "GATES OK: $accepted of $cand candidate rows accepted; $gatefail gate failures, $hashfail hash mismatches"
exit 0
