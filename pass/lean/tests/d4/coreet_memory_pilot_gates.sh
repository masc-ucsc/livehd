#!/usr/bin/env bash
# Static + differential gates over the artifacts of the focused CORE-ET memory
# pilot, with the cgen Verilog BOUND to each block's own emitted `lgdb_norm`.
#
# Same three gates the fresh 122 baseline used, in the same order and with the
# same binding rule, so a pilot row means exactly what a baseline row means:
#
#   vc_gates.py <lean> <top>                                  (static shape)
#   cert_lgraph_diff.py <lean> --quiet                        (invariant half)
#   cert_lgraph_diff.py <lean> --verilog <v> --require-verilog (REAL differential)
#
# `--require-verilog` is what stops an invariant-only run being reported as the
# full gate, and <v> is regenerated HERE from the block's own lgdb_norm rather
# than taken on trust.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$HERE/../../../.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
GATE_DIFF="$ROOT/pass/lean/scripts/cert_lgraph_diff.py"
GATE_VC="$ROOT/pass/lean/scripts/vc_gates.py"
RUN="${D4_PILOT_RUN:-$ROOT/temp/pilot_mem/out}"
OUT="${1:-$HERE/coreet_memory_pilot_gates.tsv}"

for f in "$LHD" "$GATE_DIFF" "$GATE_VC"; do
  [[ -r "$f" ]] || { echo "FATAL: missing $f" >&2; exit 2; }
done
[[ -s "$RUN/rows.tsv" ]] || { echo "FATAL: no pilot rows at $RUN/rows.tsv" >&2; exit 2; }

mkdir -p "$ROOT/temp"
TMP="$(mktemp -d "$ROOT/temp/pilotgates.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

tmp="$(mktemp "$ROOT/temp/pilotg.XXXXXX")"
{
  echo "# Gates over the focused CORE-ET memory pilot (pass.single_edge P=2 memory slots)."
  echo "# cert_differential uses cgen Verilog regenerated from each block's own lgdb_norm."
  echo "# lhd_sha256 = $(sha256sum "$LHD" | cut -c1-16)   gate_sha256 = $(sha256sum "$GATE_DIFF" | cut -c1-16)"
  echo "# gated_at   = $(date -u +%Y-%m-%dT%H:%M:%SZ)"
  printf 'module\temit_status\truntime_status\tvc_gates\tcert_invariant\tcert_differential\tcert_sha256\tverilog_sha256\tfirst_failure\n'
} > "$tmp"

ran=0; allpass=0; fails=0
while IFS=$'\t' read -r corpus m emit cert_path cert_sha rest; do
  [[ "$corpus" == "corpus" || "$corpus" == \#* || -z "$m" ]] && continue
  runst="$(awk -F'\t' -v mm="$m" '$2==mm{print $13}' "$RUN/rows.tsv")"
  if [[ "$emit" != "EMITTED" || "$runst" != "RAN" ]]; then
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\tnot emitted/ran: nothing to gate\n' "$m" "$emit" "${runst:--}" >> "$tmp"
    continue
  fi
  ran=$((ran + 1))
  lean="$RUN/blocks/$m/lean/${m}_Lgraph.lean"
  norm="$RUN/blocks/$m/lgdb_norm"
  if [[ ! -s "$lean" || ! -d "$norm" ]]; then
    fails=$((fails + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\tretained artifact or lgdb_norm missing\n' "$m" "$emit" "$runst" >> "$tmp"
    continue
  fi
  rm -rf "$TMP/v" "$TMP/w"
  if ! "$LHD" compile "lg:$norm" --top "$m" --recipe O0 --emit-dir "verilog:$TMP/v" \
        --workdir "$TMP/w" -q >"$TMP/cgen.log" 2>&1; then
    fails=$((fails + 1))
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\tcgen from lgdb_norm failed\n' "$m" "$emit" "$runst" >> "$tmp"
    continue
  fi
  cat "$TMP"/v/*.v > "$TMP/$m.v" 2>/dev/null
  [[ -s "$TMP/$m.v" ]] || { fails=$((fails + 1)); \
    printf '%s\t%s\t%s\t-\t-\t-\t-\t-\tcgen produced no verilog\n' "$m" "$emit" "$runst" >> "$tmp"; continue; }

  python3 -B "$GATE_VC" "$lean" "$m" --quiet >/dev/null 2>&1 && vc=PASS || vc=FAIL
  python3 -B "$GATE_DIFF" "$lean" --quiet >/dev/null 2>&1 && inv=PASS || inv=FAIL
  dout="$(python3 -B "$GATE_DIFF" "$lean" --verilog "$TMP/$m.v" --require-verilog 2>&1)"
  if [[ $? -eq 0 ]]; then diff=PASS; first='-'
  else diff=FAIL; first="$(grep -m1 '^FAIL' <<<"$dout")"; [[ -z "$first" ]] && first="$(tail -n1 <<<"$dout")"; fi

  if [[ "$vc" == PASS && "$inv" == PASS && "$diff" == PASS ]]; then
    allpass=$((allpass + 1))
  else
    fails=$((fails + 1))
  fi
  printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$m" "$emit" "$runst" "$vc" "$inv" "$diff" \
    "$(sha256sum "$RUN/blocks/$m/lean/${m}.dcert" 2>/dev/null | cut -c1-16)" \
    "$(sha256sum "$TMP/$m.v" | cut -c1-16)" "$first" >> "$tmp"
done < "$RUN/rows.tsv"

{
  echo "#"
  echo "# blocks that RAN            $ran"
  echo "# all three gates PASS       $allpass"
  echo "# gate failures              $fails"
} >> "$tmp"
mv -f "$tmp" "$OUT"
echo "wrote $OUT"
tail -n 4 "$OUT"
[[ "$fails" -eq 0 && "$ran" -gt 0 ]] \
  && { echo "PILOT GATES OK: $allpass of $ran blocks pass vc_gates + invariant + the real differential"; exit 0; }
echo "PILOT GATES FAILED: $fails failure(s)"; exit 1
