#!/usr/bin/env bash
# Where does a CORE-ET module's combinational cycle FIRST appear?
#
# Eleven modules are gated on a word-level comb cycle that pass.lean refuses.
# Before adding resolver rules it has to be known whether the cycle is in the
# source at all, and if not, which lowering stage invents it. This walks the
# representations in order and reports the first one that has it:
#
#   1. RTL              -- read by hand; not automatable, recorded per module
#                          in pass/lean/DIRECTION2_RESULTS.md
#   2. yosys RTLIL      -- yosys's own `scc`, on the pp-mem.il the read script
#                          writes (AFTER `flatten`, so a loop through a child
#                          module shows up here)
#   3. LGraph, no opt   -- yosys2lg output, pass.cprop NOT run
#   4. LGraph, cprop    -- after pass.cprop
#
# TWO STAGE-INTEGRITY TRAPS, both of which silently invalidate the result:
#
#  * `write_rtlil pp-mem.il` in inou_yosys_read.ys is a RELATIVE path, and yosys
#    is linked INTO lhd and runs in-process, so the dump lands in lhd's CWD --
#    not in --workdir. Looking for it under the workdir yields "no-il", and two
#    concurrent runs overwrite each other. Each run therefore gets its own CWD
#    and the file is moved out explicitly.
#  * `lhd compile lg:...` defaults to recipe O1, which RUNS pass.cprop. Emitting
#    from the "O0" graph without `--recipe O0` re-runs cprop on it, so both
#    columns measure the same thing. Check `"recipe":[...]` in the emitted JSON
#    if a result looks too uniform.
#
# Usage: scripts/cycle_provenance.sh <module>...
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
COREET_ROOT="${COREET_ROOT:-/soe/czeng14/projects/core-et}"
OUTROOT="${OUTROOT:-$ROOT/generated/provenance}"
TIMEOUT="${TIMEOUT:-1800}"

[[ -x "$LHD" ]] || { echo "FATAL: no lhd at $LHD"; exit 2; }
mkdir -p "$OUTROOT"

# pass.lean's topo walk refuses a back edge by name (the step-1 cycle guard), so
# "does this graph have a cycle" is answerable by running the emitter and
# looking for that refusal, rather than reimplementing SCC on the LGraph.
# --recipe O0 is MANDATORY here: without it this re-runs cprop and the raw
# column silently becomes a second cprop column.
lean_verdict() {  # <lgdir> <outdir> <top> -> CYCLE | CLEAN | TIMEOUT | OTHER:<codes>
  local lgdir="$1" out="$2" top="$3"
  mkdir -p "$out"
  timeout "$TIMEOUT" "$LHD" compile "lg:$lgdir" --top "$top" --recipe O0 \
    --workdir "$out/w" --result-json "$out/result.json" \
    --set formal.lean.mode=verified_compiler \
    --set formal.lean.strict=true \
    --emit-dir "lean:$out/lean" > "$out/lean.log" 2>&1
  local rc=$?
  # Record what actually ran, so a recipe regression is visible in the evidence.
  grep -oP '"recipe":\[[^]]*\]' "$out/result.json" 2>/dev/null | head -1 > "$out/recipe.txt" || true
  if grep -qiE "combinational cycle|back edge|cycle through|self-dependency" "$out/lean.log"; then
    echo "CYCLE"
  elif [[ $rc -eq 0 ]]; then echo "CLEAN"
  elif [[ $rc -eq 124 ]]; then echo "TIMEOUT"
  elif grep -q "NEGEDGE flop" "$out/lean.log"; then
    # This probe runs pass.lean straight off the LGraph and SKIPS
    # pass.single_edge, so a design with negedge/latch state refuses here for a
    # reason that has nothing to do with cycles.  Report it as unmeasured
    # rather than letting it read as "no cycle".
    echo "NEEDS-SINGLE-EDGE"
  else echo "OTHER:$(grep -oP '"code":"\K[^"]*' "$out/lean.log" | sort -u | paste -sd, -)"
  fi
}

printf '%-24s %-9s %-10s %-12s %s\n' MODULE RTLIL_SCC LG_NOOPT LG_CPROP FIRST_STAGE
for TOP in "$@"; do
  OUT="$OUTROOT/$TOP"; mkdir -p "$OUT"
  FL="$OUT/$TOP.f"
  COREET_ROOT="$COREET_ROOT" "$ROOT/scripts/coreet_filelist.sh" "$TOP" "$FL" \
    >"$OUT/filelist.log" 2>&1 || { printf '%-24s filelist-fail\n' "$TOP"; continue; }

  # --- stages 2+3: one -O0 elaboration gives both the RTLIL and the raw LGraph.
  # Run it in its own CWD so pp-mem.il is ours alone.
  CW="$OUT/noopt"; rm -rf "$CW"; mkdir -p "$CW"
  printf '// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n' > "$CW/anchor.sv"
  t0=$(date +%s)
  ( cd "$CW" && timeout "$TIMEOUT" "$LHD" compile verilog anchor.sv \
      --reader yosys-slang --top "$TOP" --recipe O0 \
      --workdir "$CW/work" --emit-dir "lg:$CW/lg" \
      --set yosys.filelist_file="$FL" --set yosys.setundef=zero \
      -- --ignore-assertions --relax-enum-conversions --allow-use-before-declare ) \
      > "$CW/compile.log" 2>&1
  rc0=$?; t1=$(date +%s)
  echo "elaborate -O0 exit=$rc0 seconds=$((t1-t0))" > "$CW/timing.txt"
  if [[ ! -d "$CW/lg" ]]; then
    printf '%-24s %s\n' "$TOP" "COMPILE-FAIL rc=$rc0 (see $CW/compile.log)"; continue
  fi

  if [[ -f "$CW/pp-mem.il" ]]; then
    yosys -p "read_rtlil $CW/pp-mem.il; scc" > "$CW/scc.log" 2>&1
    scc="$(grep -oP 'Found \K[0-9]+(?= SCCs\.)' "$CW/scc.log" | tail -1)"; scc="${scc:-?}"
  else
    scc="no-il"
  fi

  # --- stage 4: cprop, produced ONCE from the raw graph ---------------------
  CP="$OUT/cprop"; rm -rf "$CP"; mkdir -p "$CP"
  timeout "$TIMEOUT" "$LHD" compile "lg:$CW/lg" --top "$TOP" --recipe O1 \
    --workdir "$CP/work" --emit-dir "lg:$CP/lg" > "$CP/compile.log" 2>&1

  v0="$(lean_verdict "$CW/lg" "$OUT/lean_noopt" "$TOP")"
  v1="$([[ -d "$CP/lg" ]] && lean_verdict "$CP/lg" "$OUT/lean_cprop" "$TOP" || echo "no-lg")"

  first="none"
  [[ "$v1" == "CYCLE" ]] && first="pass.cprop"
  [[ "$v0" == "CYCLE" ]] && first="yosys2lg"
  [[ "$scc" =~ ^[0-9]+$ && "$scc" -gt 0 ]] && first="yosys-RTLIL(proc)"
  printf '%-24s %-9s %-10s %-12s %s\n' "$TOP" "$scc" "$v0" "$v1" "$first"
done
