#!/usr/bin/env bash
set -uo pipefail
# M3 on the 12x5 table: one unbounded invariant per benchmark row, proved on
# the projection residual by k-induction (formal/lean/scripts/sym_verify.lean,
# Sym.runInvKOf_sound_ref: the invariant over interpretDesign). The residual is
# specialized through MemVariant.projectMemVarDesignAt (env0 inlined); set
# ROUTE=plain for the original memory interpreter.
#
# The invariant is "while the design is held in reset, <output> stays 0": an
# input assumption pins every functional reset (and B05's DFT scan mode) at
# its asserting value on every cycle; the other inputs are FREE. That is a
# statement about all input sequences, not a run. Each row is ONE sym_verify
# run: the design is specialized once, then its candidate outputs are tried
# at k = 1, then at k = 2; the first PROVED is the row's lemma. Every attempt
# is recorded with its verdict and phase times; the run's wall, CPU and peak
# RSS (specialization included) go on every line of the row.
#
#   SYM_NAT=<dir with native sym_verify> sym_bench12.sh [ROW ...]
#   CADICAL=<solver>   default: the cadical of formal/lean's Lean toolchain
#
# Each row writes <OUT>/<row>.tsv; results.tsv is rebuilt from every row file
# present when a run ends, so rows may run as separate, parallel invocations.
#
# Rows read the certificates the FP column uses. B07 (CORE-ET VPU) has no
# certificate yet (its legalization decision is open) and is reported as such.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SYM_NAT="${SYM_NAT:?set SYM_NAT to the directory holding the native sym_verify}"
OUT="${OUT:-$ROOT/generated/m3_bench12}"
mkdir -p "$OUT"
CERTS="$ROOT/generated/master_port"
CADICAL="${CADICAL:-$(dirname "$(cd "$ROOT/formal/lean" && elan which lean)")/cadical}"
[[ -x "$CADICAL" ]] || { echo "no solver at $CADICAL" >&2; exit 2; }
TSV="$OUT/results.tsv"
HDR='row\tcert\tproperty\tassumptions\tk\tverdict\tspecialize_ms\tobligations_ms\tbase_clauses\tstep_clauses\tsolver_ms\tcheck_ms\trun_wall_s\trun_cpu_s\trun_maxrss_kib\n'

declare -A CERT ASM CAND
CERT[B01]="$CERTS/cov12x5/B01_coreet_dcache/attempt-20261017T235407-3634045/verified_compiler/lean/minion_dcache_top.dcert"
ASM[B01]="in:rst_ni=0";            CAND[B01]="s3_core_resp_valid_o ptw_req_valid_o l2_resp_valid_int_o s1_core_replay_next_o"
CERT[B02]="$CERTS/cov12x5/B02_cva6_hpdcache/attempt-20261017T153941-1933871/verified_compiler/lean/cva6_hpdcache_subsystem_gate.dcert"
ASM[B02]="in:rst_ni=0";            CAND[B02]="dcache_miss_o icache_miss_o dcache_flush_ack_o"
CERT[B03]="$CERTS/cov12x5/B03_coreet_icache/attempt-20261017T235418-3634733/verified_compiler/lean/icache_top.dcert"
ASM[B03]="in:rst_ni=0";            CAND[B03]="f0_l2_miss_req_valid_o f2_sram_req_valid_o esr_prefetch_done_o"
CERT[B04]="$CERTS/cov12x5/B04_cva6_mmu_tlb_ptw/attempt-20261017T153932-1933029/verified_compiler/lean/cva6_mmu_gate.dcert"
ASM[B04]="in:rst_ni=0";            CAND[B04]="dtlb_miss_o itlb_miss_o lsu_valid_o shared_tlb_flush_busy_o"
CERT[B05]="$CERTS/cov12x5/B05_coreet_etlink_axi/attempt-20261018T001217-3772559/verified_compiler/lean/shirecache_mesh_master.dcert"
ASM[B05]="in:rst_ni=0 in:noc_rst_ni=0 in:dft_i=0"; CAND[B05]="axi_ar_valid_o axi_aw_valid_o axi_w_valid_o"
CERT[B06]="$CERTS/cov12x5/B06_cva6_lsu/attempt-20261017T153938-1933502/verified_compiler/lean/load_store_unit_gate.dcert"
ASM[B06]="in:rst_ni=0";            CAND[B06]="load_valid_o store_valid_o dtlb_miss_o"
CERT[B08]="$CERTS/cov12x5/B08_coreet_intpipe/attempt-20261018T010725-335134/verified_compiler/lean/intpipe_top.dcert"
ASM[B08]="in:rst_ni=0 in:rst_d_ni=0 in:rst_w_ni=0"; CAND[B08]="ex_dcache_req_valid flb_neigh_req_valid TE_exception"
CERT[B09]="$CERTS/cov12x5/B09_cva6_issue_scoreboard/verified_compiler_mw/lean/issue_stage_gate.dcert"
ASM[B09]="in:rst_ni=0";            CAND[B09]="alu_valid_o branch_valid_o lsu_valid_o"
CERT[B10]="$CERTS/cov12x5/B10_cva6_frontend/verified_compiler_mw/lean/frontend_gate.dcert"
ASM[B10]="in:rst_ni=0";            CAND[B10]="fetch_entry_valid_o"
CERT[B11]="$CERTS/cov12x5/B11_coreet_tlb/attempt-20261017T235428-3635139/verified_compiler/lean/minion_tlb.dcert"
ASM[B11]="in:rst_ni=0";            CAND[B11]="ptw_req_valid_o ptw_invalidate_o vm_enabled_o"
CERT[B12]="$CERTS/memroute_pico_maskfix_20261013/lean/picorv32.dcert"
ASM[B12]="in:resetn=0";            CAND[B12]="mem_valid trap trace_valid"

ROWS=("$@")
[[ ${#ROWS[@]} -gt 0 ]] || ROWS=(B01 B02 B03 B04 B05 B06 B07 B08 B09 B10 B11 B12)
for row in "${ROWS[@]}"; do
  if [[ "$row" == B07 ]]; then
    printf '%s\t-\t-\t-\t-\tNO CERTIFICATE (legalization decision open)\t-\t-\t-\t-\t-\t-\t-\t-\t-\n' "$row" > "$OUT/$row.tsv"
    echo "$row: no certificate (legalization decision open)"; continue
  fi
  cert="${CERT[$row]}"
  [[ -f "$cert" ]] || { echo "$row: missing $cert"; continue; }
  cands=""
  for out in ${CAND[$row]}; do cands="${cands:+$cands,}!$out"; done
  log="$OUT/${row}.log"
  # shellcheck disable=SC2086
  /usr/bin/time -f '%e %U %M' -o "$log.time" nice -n19 ionice -c3 "$SYM_NAT/sym_verify" "$cert" "$cands" \
    ${ASM[$row]} k:1,2 sf:200000 wf:5000 "route:${ROUTE:-var}" "solver:$CADICAL" "work:$OUT/work_${row}" > "$log" 2>&1
  read -r wall cpu rss < <(tail -1 "$log.time")
  python3 - "$row" "${cert##*/}" "${ASM[$row]}" "$wall" "$cpu" "$rss" "$log" > "$OUT/$row.tsv" <<'PY2'
import re, sys
row, cert, asm, wall, cpu, rss, log = sys.argv[1:]
text = open(log).read()
spec = re.search(r"residual specialized(?: \([^)]*\))? and checked in (\d+) ms", text)
spec = spec.group(1) if spec else "-"
# per attempt: [tok k=K] lines, or the unprefixed lines of a single attempt
att = {}
order = []
for line in text.splitlines():
    m = re.match(r"\s+\[(\S+) k=(\d+)\] (.*)", line)
    key, body = ((m.group(1), m.group(2)), m.group(3)) if m else (None, None)
    if key is None:
        continue
    if key not in att:
        att[key] = {}; order.append(key)
    a = att[key]
    if (mm := re.match(r"obligations built in (\d+) ms", body)): a["obl"] = mm.group(1)
    if (mm := re.match(r"base: (\d+) clauses; bit-blast\+CNF(?:\+write)? (\d+) ms, solver (\d+) ms", body)):
        a["bc"] = mm.group(1); a["sol"] = int(mm.group(3))
    if (mm := re.match(r"step: (\d+) clauses; bit-blast\+CNF(?:\+write)? (\d+) ms, solver (\d+) ms", body)):
        a["sc"] = mm.group(1); a["sol"] = a.get("sol", 0) + int(mm.group(3))
    if (mm := re.match(r"verified check .* in (\d+) ms", body)): a["chk"] = mm.group(1)
    if (mm := re.match(r"RESULT: (.*)", body)): a["v"] = mm.group(1).split(" --")[0].split(":")[0].strip()
if not order:
    v = re.search(r"RESULT: (.*)", text)
    print(f"{row}\t{cert}\t-\t{asm}\t-\t{v.group(1).split(' --')[0] if v else 'ERROR'}\t{spec}\t-\t-\t-\t-\t-\t{wall}\t{cpu}\t{rss}")
for (tok, k) in order:
    a = att[(tok, k)]
    print(f"{row}\t{cert}\t{tok}\t{asm}\t{k}\t{a.get('v', 'ERROR')}\t{spec}\t{a.get('obl', '-')}\t{a.get('bc', '-')}\t{a.get('sc', '-')}\t{a.get('sol', '-')}\t{a.get('chk', '-')}\t{wall}\t{cpu}\t{rss}")
PY2
  echo "$row: $(grep -o 'SUMMARY: .*' "$log" | head -1 || true) (${wall}s, ${rss} KiB)"
done
{ printf "$HDR"; for f in "$OUT"/B[0-9][0-9].tsv; do if [[ -f "$f" ]]; then cat "$f"; fi; done; } > "$TSV.tmp.$$" \
  && mv -f "$TSV.tmp.$$" "$TSV"
echo "results: $TSV"
