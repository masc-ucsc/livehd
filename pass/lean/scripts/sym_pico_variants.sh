#!/usr/bin/env bash
set -uo pipefail
# M3 on the PicoRV32 pipeline variants: k-induction, refinement and stuttering
# through the projection residual (sym_verify, sym_refine).
#
#   base             picorv32 as configured in pico_lean (TWO_CYCLE_COMPARE=0)
#   two_cycle_cmp    TWO_CYCLE_COMPARE=1: a registered branch compare
#                    (alu_out_0_q) and one extra EXEC cycle per branch (alu_wait)
#
# Both RTL files are copied with observer outputs (sym_observe.py; the source
# is not modified): obs_pc = reg_pc and obs_state = cpu_state on both, and on
# two_cycle_cmp the stutter witness
#     sym_adv = !(cpu_state == EXEC && alu_wait)
# -- the specification (base) holds on the extra compare cycle and steps on
# every other cycle. Each copy goes through the real flow (yosys-slang ->
# LGraph -> single_edge -> DCERT2), then:
#   1. invariants: trap and mem_valid stay 0 while resetn is held low (both)
#   2. base refines base, every output paired, next states equal (stateq)
#   3. two_cycle_cmp refines base up to stuttering (adv:sym_adv), memory
#      interface + observers paired, k = 1, 2, 3
# Run 3 is the variant refinement the user will drive to a proof; this script
# records whatever verdict the machinery reaches (PROVED, NOT K-INDUCTIVE, or
# a base counterexample) with its resources. Results: <OUT>/results.tsv.
#
#   SYM_NAT=<dir with native sym_verify and sym_refine> sym_pico_variants.sh [STEP ...]
#   PICO_RTL=<dir with picorv32_base.v, picorv32_two_cycle_cmp.v>
#                 default: the pico_lean checkout beside this repository

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
SYM_NAT="${SYM_NAT:?set SYM_NAT to the directory holding the native sym_verify and sym_refine}"
PICO_RTL="${PICO_RTL:-$ROOT/../pico_lean/rtl}"
OUT="${OUT:-$ROOT/generated/m3_pico_variants}"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
CADICAL="${CADICAL:-$(dirname "$(cd "$ROOT/formal/lean" && elan which lean)")/cadical}"
[[ -x "$LHD" ]] || { echo "build //lhd:lhd first" >&2; exit 2; }
[[ -x "$CADICAL" ]] || { echo "no solver at $CADICAL" >&2; exit 2; }
mkdir -p "$OUT"

STEPS=("$@")
[[ ${#STEPS[@]} -gt 0 ]] || STEPS=(certs inv refl stutter)

B=picorv32_base
T=picorv32_two_cycle_cmp
CB="$OUT/${B}_lean/$B.dcert"
CT="$OUT/${T}_lean/$T.dcert"

certs() {
  python3 "$SCRIPT_DIR/sym_observe.py" "$PICO_RTL/$B.v" "$B" "$OUT/$B.v" \
    "obs_pc:32=reg_pc" "obs_state:8=cpu_state" || return 1
  python3 "$SCRIPT_DIR/sym_observe.py" "$PICO_RTL/$T.v" "$T" "$OUT/$T.v" \
    "sym_adv:1=!(cpu_state == 8'b00001000 && alu_wait)" "obs_pc:32=reg_pc" "obs_state:8=cpu_state" || return 1
  for V in $B $T; do
    "$LHD" compile "$OUT/$V.v" --reader yosys-slang --top "$V" --set yosys.setundef=zero \
      --emit-dir "lg:$OUT/${V}_lg" --workdir "$OUT/${V}_cw" > "$OUT/$V.compile.log" 2>&1 \
      || { echo "FAIL: compile $V"; return 1; }
    "$LHD" pass single_edge --top "$V" "lg:$OUT/${V}_lg" --emit-dir "lg:$OUT/${V}_se" \
      --workdir "$OUT/${V}_sw" > "$OUT/$V.se.log" 2>&1 || { echo "FAIL: single_edge $V"; return 1; }
    "$LHD" compile "lg:$OUT/${V}_se" --top "$V" --workdir "$OUT/${V}_lw" --emit-dir "lean:$OUT/${V}_lean" \
      --set formal.lean.mode=verified_compiler > "$OUT/$V.lean.log" 2>&1 \
      || { grep -o '"message":"[^"]*' "$OUT/$V.lean.log" | head -3; echo "FAIL: certificate $V"; return 1; }
    echo "certificate: $OUT/${V}_lean/$V.dcert (sha256 $(sha256sum < "$OUT/${V}_lean/$V.dcert" | cut -c1-16))"
  done
}

# run <tag> <driver> args...: one timed run; its verdict lines go to <tag>.tsv
run() {
  local tag=$1 drv=$2; shift 2
  local log="$OUT/$tag.log"
  /usr/bin/time -f '%e %U %M' -o "$log.time" nice -n19 ionice -c3 "$SYM_NAT/$drv" "$@" \
    sf:200000 wf:5000 "solver:$CADICAL" "work:$OUT/work_$tag" > "$log" 2>&1
  local wall cpu rss
  read -r wall cpu rss < <(tail -1 "$log.time")
  grep -o '\(\[[^]]*\] \)\?RESULT: [A-Z (),=0-9-]*' "$log" | sed 's/ *$//' | while read -r line; do
    printf '%s\t%s\t%s\t%s\t%s\n' "$tag" "$line" "$wall" "$cpu" "$rss"
  done > "$OUT/$tag.tsv"
  echo "$tag: $(tail -1 "$OUT/$tag.tsv" | cut -f2) (${wall}s, ${rss} KiB)"
}

PAIRS_MEM="pair:mem_valid=mem_valid pair:mem_instr=mem_instr pair:mem_addr=mem_addr \
pair:mem_wdata=mem_wdata pair:mem_wstrb=mem_wstrb pair:trap=trap pair:obs_pc=obs_pc pair:obs_state=obs_state"
PAIRS_ALL="$PAIRS_MEM pair:mem_la_read=mem_la_read pair:mem_la_write=mem_la_write \
pair:mem_la_addr=mem_la_addr pair:mem_la_wdata=mem_la_wdata pair:mem_la_wstrb=mem_la_wstrb"

for s in "${STEPS[@]}"; do
  case "$s" in
    certs) certs || exit 1 ;;
    inv)
      run inv_base sym_verify "$CB" '!trap,!mem_valid' in:resetn=0 k:1,2
      run inv_two  sym_verify "$CT" '!trap,!mem_valid' in:resetn=0 k:1,2 ;;
    refl)
      # shellcheck disable=SC2086
      run refl_base sym_refine "$CB" "$CB" $PAIRS_ALL stateq k:1 ;;
    stutter)
      # shellcheck disable=SC2086
      run stutter_two_base sym_refine "$CT" "$CB" $PAIRS_MEM adv:sym_adv k:1,2,3 ;;
    *) echo "unknown step $s (certs inv refl stutter)"; exit 2 ;;
  esac
done
{ printf 'run\tverdict\twall_s\tcpu_s\tmaxrss_kib\n'
  for f in "$OUT"/inv_*.tsv "$OUT"/refl_*.tsv "$OUT"/stutter_*.tsv; do if [[ -f "$f" ]]; then cat "$f"; fi; done
} > "$OUT/results.tsv.tmp.$$" && mv -f "$OUT/results.tsv.tmp.$$" "$OUT/results.tsv"
echo "results: $OUT/results.tsv"
