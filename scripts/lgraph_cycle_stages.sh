#!/usr/bin/env bash
# WHERE does a combinational cycle first appear?  Dump one module's LGraph at
# every stage of the normal CORE-ET route and run the SCC reporter on each.
#
#   s0_yosys2lg    RTL -> LGraph, recipe O0 (NO cprop)
#   s1_cprop       the same read at recipe O1 -- what run_coreet_module_lean.sh
#                  calls lgdb_raw, already post-cprop
#   s2_singleedge  pass.single_edge output -- what pass.lean actually consumes
#
# Each stage is dumped with `lhd tool cat --target all --max 0` and reported
# under all three cut policies, so one run answers "is there a cycle at this
# stage" and "does it close only through a memory read".
#
# DIAGNOSTIC ONLY: it runs the shipped binary and reads its output, changes no
# pass, and must stay runnable on a graph pass.lean refuses.
#
# FAIL-CLOSED.  Every command's exit status is recorded and printed.  A stage
# that did not build, did not dump, or whose reporter refused the dump prints
# FAILED -- never `0`, which would read as "no cycle here".  The script exits
# non-zero if any cell failed, so a sweep cannot be mistaken for a clean sweep.
#
# Usage: scripts/lgraph_cycle_stages.sh <module> [<module>...]
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
COREET_ROOT="${COREET_ROOT:-/soe/czeng14/projects/core-et}"
OUTROOT="${OUTROOT:-$ROOT/generated/provenance/cycle_stages}"
TIMEOUT="${TIMEOUT:-1800}"
PY="${PY:-python3}"

[[ -x "$LHD" ]] || { echo "FATAL: missing $LHD"; exit 2; }
mkdir -p "$OUTROOT"
bad=0

printf '%-30s %-14s %7s %7s %10s %12s %9s\n' \
       MODULE STAGE BUILD_RC DUMP_RC SCC_LEAN SCC_LEANMEM SCC_NONE
for TOP in "$@"; do
  OD="$OUTROOT/$TOP"; mkdir -p "$OD"
  F="$OD/$TOP.f"
  if ! COREET_ROOT="$COREET_ROOT" "$ROOT/scripts/coreet_filelist.sh" "$TOP" "$F" \
         > "$OD/filelist.log" 2>&1; then
    printf '%-30s %-14s %7s\n' "$TOP" "(filelist)" FAILED; bad=1; continue
  fi
  printf '// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n' > "$OD/anchor.sv"

  declare -A BUILD_RC=()
  for recipe in O0 O1; do
    if [[ "$recipe" == O0 ]]; then stage=s0_yosys2lg; else stage=s1_cprop; fi
    rm -rf "$OD/$stage.lg" "$OD/$stage.cwd"; mkdir -p "$OD/$stage.cwd"
    # `write_rtlil pp-mem.il` inside inou_yosys_read.ys is RELATIVE and yosys is
    # linked into lhd, so give each read its own cwd or concurrent reads collide.
    ( cd "$OD/$stage.cwd" && timeout "$TIMEOUT" nice -n19 ionice -c3 "$LHD" compile verilog "$OD/anchor.sv" \
        --reader yosys-slang --top "$TOP" --recipe "$recipe" \
        --workdir "$OD/$stage.work" --emit-dir "lg:$OD/$stage.lg" \
        --set yosys.filelist_file="$F" --set yosys.setundef=zero \
        -- --ignore-assertions --relax-enum-conversions --allow-use-before-declare ) \
      > "$OD/$stage.build.log" 2>&1
    BUILD_RC[$stage]=$?
  done

  rm -rf "$OD/s2_singleedge.lg"
  timeout "$TIMEOUT" nice -n19 ionice -c3 "$LHD" pass single_edge --top "$TOP" "lg:$OD/s1_cprop.lg" \
    --emit-dir "lg:$OD/s2_singleedge.lg" --set multi_clock=true \
    --workdir "$OD/se.work" > "$OD/s2_singleedge.build.log" 2>&1
  BUILD_RC[s2_singleedge]=$?

  for stage in s0_yosys2lg s1_cprop s2_singleedge; do
    brc="${BUILD_RC[$stage]}"
    drc="-"; declare -A SC=([lean]=FAILED [lean-plus-mem]=FAILED [none]=FAILED)
    if [[ "$brc" -eq 0 && -d "$OD/$stage.lg" && -n "$(ls -A "$OD/$stage.lg" 2>/dev/null)" ]]; then
      nice -n19 ionice -c3 "$LHD" tool cat "lg:$OD/$stage.lg" --top "$TOP" --target all --max 0 \
        --diag-fmt jsonl > "$OD/$stage.jsonl" 2> "$OD/$stage.dump.err"
      drc=$?
      # A NONEMPTY dump from a FAILED `tool cat` is still a partial dump; never
      # analyse one.  (`lhd` streams, so a crash mid-graph leaves bytes behind.)
      if [[ "$drc" -eq 0 && -s "$OD/$stage.jsonl" ]]; then
        for c in lean lean-plus-mem none; do
          "$PY" "$ROOT/scripts/lgraph_scc.py" "$OD/$stage.jsonl" --stage "$stage" --cut "$c" \
            --max-report 4 --max-members 40 > "$OD/$stage.scc.$c.txt" 2>&1
          prc=$?   # captured on its own line: `$?` after anything else is that thing's status
          if [[ "$prc" -eq 0 ]]; then
            SC[$c]="$(grep -oP '^  SCCS=\K[0-9]+' "$OD/$stage.scc.$c.txt" | head -1)"
            [[ -n "${SC[$c]}" ]] || SC[$c]=FAILED
          fi
        done
      elif [[ "$drc" -eq 0 ]]; then
        drc="empty"
      fi
    fi
    for c in lean lean-plus-mem none; do
      [[ "${SC[$c]}" == FAILED ]] && bad=1
    done
    [[ "$brc" -eq 0 ]] || bad=1
    printf '%-30s %-14s %7s %7s %10s %12s %9s\n' \
      "$TOP" "$stage" "$brc" "$drc" "${SC[lean]}" "${SC[lean-plus-mem]}" "${SC[none]}"
  done
done

if [[ "$bad" -ne 0 ]]; then
  echo "FATAL: at least one cell above is FAILED or built non-zero -- this table is NOT a clean sweep" >&2
  exit 3
fi
