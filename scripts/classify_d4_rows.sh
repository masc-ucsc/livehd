#!/usr/bin/env bash
# Classify a sweep's rows by the LAST STAGE each block reached.
#
# A generated wrapper is not runtime coverage, and neither is an emitted
# certificate.  This prints one line per outcome over the FULL declared
# denominator, so a summary can never be assembled by picking the flattering
# subset.  It also re-checks the row contract rather than trusting it.
#
# Usage: scripts/classify_d4_rows.sh <rows.tsv> [provenance.txt]
set -uo pipefail
ROWS="${1:?usage: classify_d4_rows.sh <rows.tsv> [provenance.txt]}"
PROV="${2:-$(dirname "$ROWS")/provenance.txt}"
die() { printf 'FATAL: %s\n' "$*" >&2; exit 2; }
[[ -s "$ROWS" ]] || die "$ROWS is missing or empty"

n=$(( $(wc -l < "$ROWS") - 1 ))
uniq_n=$(tail -n +2 "$ROWS" | cut -f2 | sort -u | wc -l)
badshape=$(awk -F'\t' 'NR>1 && NF!=18' "$ROWS" | wc -l)
[[ "$n" -eq "$uniq_n" ]] || die "$n rows but $uniq_n unique modules"
[[ "$badshape" -eq 0 ]] || die "$badshape rows do not have 18 fields"

# Outcome, from the row itself.  Order matters: the FIRST stage that failed is
# the outcome, so a block is never credited with a stage it did not reach.
cls() {
  awk -F'\t' 'NR>1 {
    emit=$3; parse=$10; ok=$12; rt=$13; diag=$16;
    if (emit == "NO_WRAPPER")                       { print "NO_WRAPPER"; next }
    # No authoritative kernel verdict: the block could not be certified either
    # way, so it was not run.  NEVER runtime coverage.
    if (emit == "NO_VERDICT")                       { print "NO_VERDICT:evidence-unavailable"; next }
    # The emitter itself failed.  A certificate may exist on disk; it was never
    # run and is never coverage.  Timeout is kept distinct from other failures.
    if (emit == "EMIT_FAILED") {
      if (diag ~ /emission timeout/)                { print "EMIT_FAILED:timeout"; next }
                                                      print "EMIT_FAILED:nonzero-rc"; next }
    if (emit == "NO_CERT") {
      if (diag ~ /^resource-limit:/)                { print "NO_CERT:resource-limit"; next }
      if (diag ~ /emission timeout/)                { print "NO_CERT:timeout"; next }
                                                      print "NO_CERT:front-end"; next }
    if (parse == "NO_ROW")                          { print "NO_ROW"; next }
    if (parse == "READ_ERROR")                      { print "READ_ERROR"; next }
    if (parse == "PARSE_ERROR")                     { print "PARSE_ERROR"; next }
    if (ok != "true")                               { print "REFUSED:compilesOk=false"; next }
    if (rt == "RUN_ERROR")                          { print "RUN_ERROR"; next }
    if (rt == "RAN")                                { print "RAN"; next }
                                                      print "OTHER:" rt }' "$ROWS"
}

echo "rows=$n  unique=$uniq_n  fields=18  rows_wrong_shape=$badshape"
[[ -s "$PROV" ]] && grep -E '^(reconciled|jobs_requested|execution|stack_limit_kb|emit_timeout_s|run_timeout_s|emit_max_rss_kb|emit_limit_kind)=' "$PROV" | sed 's/^/  /'
echo
printf '%-28s %s\n' "OUTCOME" "COUNT"
cls | sort | uniq -c | sort -rn | awk '{printf "%-28s %s\n", $2, $1}'
echo
ran=$(cls | grep -c '^RAN$')
printf 'RUNTIME COVERAGE: %s of %s declared blocks reached PARSED / compiles_ok=true / RAN\n' "$ran" "$n"
echo
# The sweep already bounds the diagnostic column, and this truncates further, so
# it is a summary -- the full text is in the block's own logs.
echo "NON-RAN ROWS (diagnostic column, truncated):"
awk -F'\t' 'NR>1 && !($10=="PARSED" && $12=="true" && $13=="RAN") {
  printf "  %-26s %-10s %-12s %-9s %s\n", $2, $3, $10, $13, substr($16,1,96) }' "$ROWS"
echo
echo "CERTIFICATE HASHES RETAINED: $(awk -F'\t' 'NR>1 && $5!="-"' "$ROWS" | wc -l) of $(awk -F'\t' 'NR>1 && $4!="-"' "$ROWS" | wc -l) emitted"
