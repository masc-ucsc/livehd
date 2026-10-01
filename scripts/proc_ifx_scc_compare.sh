#!/usr/bin/env bash
# Does `proc -ifx` invent the combinational cycles that gate 11 CORE-ET modules?
#
# `inou_yosys_read.ys` runs `proc -ifx` instead of `proc`, deliberately: the
# comment there says proc_rmdead's BitPatternPool fragments into millions of
# cubes on wide casex/casez decoders and effectively hangs.  But proc_rmdead is
# ALSO what prunes the "no branch matched" arm of a full case, and without that
# pruning the arm survives as `Y = sel ? a : Y` -- a self-referencing mux, which
# yosys `scc` reports as a logic loop.  Whether that arm is SPURIOUS or is the
# intended X behaviour is NOT settled by this script; see below.
#
# Measured on txfma_e5: `proc -ifx` -> 1 SCC (two pmuxtree muxes on
# elxd_res_f5a_h, a 1-bit-selector case covering both 1'b0 and 1'b1, so FULL);
# plain `proc` -> 0 SCCs.
#
# WHAT THIS DOES AND DOES NOT SHOW. It shows a LOWERING DIFFERENTIAL: the SCC is
# retained under -ifx and removed when proc_rmdead runs. It does NOT show which
# 4-state/X semantics is faithful. yosys documents -ifx as the Verilog
# simulation behaviour for undefined conditions, and these RTL files
# deliberately suppress CASEINCOMPLETE, so the hold arm may be what the author
# meant under X. The Lean model is 2-state, so plain proc may well be valid on
# the model domain -- but that needs the defined-input equivalence gate (the
# LEC leg in run_coreet_module_lean.sh), not this table.
#
# This elaborates each module both ways and reports the SCC count, so the claim
# can be checked per module instead of generalized from one.  It also records
# the wall time of each, because the hang risk the comment warns about is the
# reason the flag is there and any fix has to answer it.
#
# Usage: scripts/proc_ifx_scc_compare.sh <module>...
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
LHD="${LHD:-$ROOT/bazel-bin/lhd/lhd}"
COREET_ROOT="${COREET_ROOT:-/soe/czeng14/projects/core-et}"
OUTROOT="${OUTROOT:-$ROOT/generated/provenance}"
TIMEOUT="${TIMEOUT:-900}"
PLAIN="$OUTROOT/read_plain_proc.ys"

mkdir -p "$OUTROOT"
sed 's/^proc -ifx$/proc/' "$ROOT/inou/yosys/inou_yosys_read.ys" > "$PLAIN"
grep -qx 'proc' "$PLAIN" || { echo "FATAL: the -ifx substitution did not apply"; exit 2; }

# `write_rtlil pp-mem.il` in the script is RELATIVE, and yosys is linked into
# lhd and runs in-process, so the dump lands in lhd's cwd.  Each run therefore
# gets its own cwd or concurrent runs would overwrite each other's RTLIL.
elaborate() {  # <top> <tag> <script-or-empty> -> echoes "<sccs> <seconds>"
  local top="$1"
  local tag="$2"
  local script="$3"
  local d="$OUTROOT/$top/$tag"
  rm -rf "$d"; mkdir -p "$d"
  printf '// Empty anchor. Real CORE-ET sources arrive via yosys.filelist_file.\n' > "$d/anchor.sv"
  local t0; t0=$(date +%s)
  ( cd "$d" && timeout "$TIMEOUT" "$LHD" compile verilog anchor.sv \
      --reader yosys-slang --top "$top" --recipe O0 \
      --workdir "$d/work" --emit-dir "lg:$d/lg" \
      --set yosys.filelist_file="$OUTROOT/$top/$top.f" \
      --set yosys.setundef=zero \
      ${script:+--set yosys.script="$script"} \
      -- --ignore-assertions --relax-enum-conversions --allow-use-before-declare ) \
    > "$d/compile.log" 2>&1
  local rc=$? t1; t1=$(date +%s)
  if [[ ! -f "$d/pp-mem.il" ]]; then echo "no-il($rc) $((t1-t0))"; return; fi
  yosys -p "read_rtlil $d/pp-mem.il; scc" > "$d/scc.log" 2>&1
  echo "$(grep -oP 'Found \K[0-9]+(?= SCCs\.)' "$d/scc.log" | tail -1) $((t1-t0))"
}

# Zero RTLIL SCCs is NECESSARY but not SUFFICIENT for a usable certificate, so
# the plain-proc graph is also put through pass.lean. --recipe O0 is mandatory:
# the default is O1 and would re-run pass.cprop, making this a different test
# from the one it claims to be.
# NAMING. A zero exit from pass.lean proves the CERTIFICATE TEXT WAS EMITTED.
# It is NOT "Lean accepted": no `lake build`, no checkDesign, no simulator run
# has happened at this point.  The 122-CORE-ET / 30-CVA6 milestone needs that
# later pipeline acceptance, so this column is deliberately called CERT_EMIT.
lean_on_plain() {  # <top> -> CERT-EMITTED | CYCLE | NEEDS-SINGLE-EDGE
                   #          | PLAIN-COMPILE-FAIL:<code> | TIMEOUT | ERR:<codes>
  # ONE NAME PER `local`.  Bash expands every word of a `local` command BEFORE
  # performing any of its assignments, so `local top="$1" d="$OUTROOT/$top/..."`
  # expands $top while it is still unset -- which under `set -u` aborts the
  # helper on every row and silently blanks this column.  That exact bug
  # invalidated a full 11-row table; see generated/provenance/invalid/ and
  # lhd/tests/proc_ifx_compare_helper_test.sh.
  local top="$1"
  local d="$OUTROOT/$top/plain"
  # The plain-proc ELABORATION can itself fail -- measured: intpipe_csr_file and
  # minion_dcache_top both abort with latch-contract rule C under plain proc
  # while compiling cleanly under -ifx, because proc_dlatch infers a real
  # $dlatch where -ifx left a comb self-loop.  That is a compile result, not a
  # pass.lean result, and must never be reported as one.
  if [[ ! -d "$d/lg" ]] || [[ -z "$(ls -A "$d/lg" 2>/dev/null)" ]]; then
    local code
    code="$(grep -oP '"code":"\K[^"]*' "$d/compile.log" 2>/dev/null | sort -u | paste -sd, -)"
    echo "PLAIN-COMPILE-FAIL:${code:-unknown}"
    return
  fi
  timeout "$TIMEOUT" "$LHD" compile "lg:$d/lg" --top "$top" --recipe O0 \
    --workdir "$d/lean_w" --emit-dir "lean:$d/lean" \
    --set formal.lean.mode=verified_compiler --set formal.lean.strict=true \
    > "$d/lean.log" 2>&1
  local rc=$?
  if   [[ $rc -eq 0 ]]; then echo "CERT-EMITTED"
  elif [[ $rc -eq 124 ]]; then echo "TIMEOUT"
  elif grep -qiE "combinational cycle|back edge|self-dependency" "$d/lean.log"; then echo "CYCLE"
  elif grep -qE "unsupported certificate op \`latch\`|NEGEDGE flop" "$d/lean.log"; then
    # This comparator runs pass.lean straight off the LGraph and SKIPS
    # pass.single_edge, which is what normalizes latches and negedge state away.
    # A stateful wrapper therefore refuses here for a reason that has nothing to
    # do with the lowering under test.  Report it as unmeasured and validate via
    # run_coreet_module_lean.sh with YOSYS_SCRIPT instead.
    echo "NEEDS-SINGLE-EDGE"
  else echo "ERR:$(grep -oP '"code":"\K[^"]*' "$d/lean.log" | sort -u | paste -sd, -)"
  fi
}

# Sourcing with PROC_IFX_LIB_ONLY=1 defines the helpers and stops, so a test can
# call them without running a multi-hour sweep.
if [[ "${PROC_IFX_LIB_ONLY:-0}" == "1" ]]; then
  return 0 2>/dev/null || exit 0
fi

blank_rows=0
printf '%-26s %8s %7s %9s %7s %-26s %s\n' MODULE IFX_SCC IFX_S PLAIN_SCC PLAIN_S CERT_EMIT EVIDENCE
for TOP in "$@"; do
  mkdir -p "$OUTROOT/$TOP"
  COREET_ROOT="$COREET_ROOT" "$ROOT/scripts/coreet_filelist.sh" "$TOP" "$OUTROOT/$TOP/$TOP.f" \
    >"$OUTROOT/$TOP/filelist.log" 2>&1 || { printf '%-26s filelist-fail\n' "$TOP"; continue; }
  read -r a as <<<"$(elaborate "$TOP" ifx "")"
  read -r b bs <<<"$(elaborate "$TOP" plain "$PLAIN")"
  # Neutral wording: this column reports WHAT WAS MEASURED, not which lowering
  # is semantically right.
  v="?"
  if [[ "$a" =~ ^[0-9]+$ && "$b" =~ ^[0-9]+$ ]]; then
    if   [[ "$a" -gt 0 && "$b" -eq 0 ]]; then v="SCC retained by -ifx; removed by proc_rmdead/plain proc"
    elif [[ "$a" -gt 0 && "$b" -gt 0 ]]; then v="SCC present under both lowerings"
    elif [[ "$a" -eq 0 && "$b" -eq 0 ]]; then v="no RTLIL SCC under either lowering"
    else                                      v="SCC only under plain proc"; fi
  fi
  l="$(lean_on_plain "$TOP")"
  # A BLANK field is the failure mode that invalidated an entire table once: the
  # helper died, its capture was empty, and the row still printed as if it had
  # been measured.  Never emit a blank; make it loud instead.
  if [[ -z "${l// }" ]]; then  # a blank CERT_EMIT field is never acceptable
    l="HELPER-FAILED"
    blank_rows=$((blank_rows + 1))
  fi
  printf '%-26s %8s %7s %9s %7s %-26s %s\n' "$TOP" "$a" "$as" "$b" "$bs" "$l" "$v"
done

if [[ "$blank_rows" -gt 0 ]]; then
  echo "FATAL: $blank_rows row(s) had a blank LEAN_PLAIN -- this table is NOT valid evidence" >&2
  exit 3
fi
