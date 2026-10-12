#!/usr/bin/env bash
# SERIAL COVERAGE QUEUE for the 12x5 table: one CVA6 row at a time, every
# currently usable approach-native route, metrics recorded by fp_cov_run.sh.
# Phases are kept separate: upstream RTL elaboration, single_edge, per-approach
# emission, and the generation check (elaboration) are four measurements, never
# one continuous wall time.
#   fp_cov_queue.sh <row-id> <top> <filelist>
set -uo pipefail
R=/mada/users/czeng14/projects/livehd-pass-lean-master
COV=$R/generated/master_port/cov12x5
LOPT=/var/tmp/bazel_czeng14/_bazel_czeng14/8036432c6fc5c16a245c44de062c5637/execroot/_main/bazel-out/k8-opt/bin/lhd/lhd
ROW=${1:?row}; TOP=${2:?top}; FL=${3:?filelist}
B=$COV/$ROW
mkdir -p "$B" "$COV/tmp"
export TMPDIR=$COV/tmp TMP=$COV/tmp TEMP=$COV/tmp
ANCHOR=$B/anchor.sv
printf '// Empty anchor. Real sources arrive via yosys.filelist_file.\n' > "$ANCHOR"
CR="bash $R/pass/lean/scripts/fp_cov_run.sh"

$CR "$ROW" s1_compile_lg "$B/s1_compile" 32G 3600 -- \
  "$LOPT" compile verilog "$ANCHOR" --reader yosys-slang --top "$TOP" \
  --workdir "$B/s1_compile/work" --result-json "$B/s1_compile/result.json" \
  --emit-dir lg:"$B/s1_compile/lg" --set yosys.filelist_file="$FL" \
  --set yosys.setundef=zero -- --ignore-assertions || true
[ -d "$B/s1_compile/lg" ] || { echo "$ROW: STOP after s1"; exit 1; }

$CR "$ROW" s2_single_edge "$B/s2_single_edge" 32G 3600 -- \
  "$LOPT" pass single_edge --top "$TOP" lg:"$B/s1_compile/lg" \
  --emit-dir lg:"$B/s2_single_edge/lg" --workdir "$B/s2_single_edge/work" \
  --result-json "$B/s2_single_edge/result.json" || true
[ -d "$B/s2_single_edge/lg" ] || { echo "$ROW: STOP after single_edge"; exit 1; }

$CR "$ROW" verified_compiler "$B/verified_compiler" 32G 3600 -- \
  "$LOPT" compile lg:"$B/s2_single_edge/lg" --top "$TOP" \
  --workdir "$B/verified_compiler/work" --result-json "$B/verified_compiler/result.json" \
  --emit-dir lean:"$B/verified_compiler/lean" --set formal.lean.mode=verified_compiler || true

$CR "$ROW" legacy "$B/legacy" 32G 3600 -- \
  "$LOPT" compile lg:"$B/s2_single_edge/lg" --top "$TOP" \
  --workdir "$B/legacy/work" --result-json "$B/legacy/result.json" \
  --emit-dir lean:"$B/legacy/lean" --set formal.lean.emit_cert=true \
  --set formal.lean.cert_wf=eval || true

echo "$ROW: emission phases done"
