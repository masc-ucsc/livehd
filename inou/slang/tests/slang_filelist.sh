#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--reader slang -- -F filelist.f` passthrough test (see inou/slang/README.md).
# The verilog sources ride the raw slang driver args via a `-F` command file
# with NO positional `*.v` input; the compile must aggregate every file the
# list names (here a top + its leaf submodule) into one unit, emit verilog,
# and pass LEC against the same sources.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x $LHD ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAILED: slang_filelist.sh could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

DIR=inou/slang/tests/filelist
FLIST=$DIR/foo.f
if [ ! -r "$FLIST" ]; then
  echo "FAILED: filelist not found at $FLIST (cwd=$(pwd))"
  exit 1
fi

WD=tmp_slang/filelist
rm -rf "$WD"
mkdir -p "$WD"

# Sources come ENTIRELY from the `-F` command file (no positional .v input).
${LHD} compile --reader slang --top foo \
  --emit-dir verilog:"$WD"/v/ --workdir "$WD"/w -q \
  -- -F "$FLIST" >"$WD"/compile.log 2>&1 || {
  echo "FAIL(filelist): slang -F filelist compile failed"
  cat "$WD"/compile.log
  exit 1
}

cat "$WD"/v/*.v >"$WD"/all.v 2>/dev/null
if [ ! -s "$WD"/all.v ]; then
  echo "FAIL(filelist): no verilog emitted"
  exit 1
fi

# LEC the emitted netlist against the same two sources.
cat "$DIR"/sub.v "$DIR"/foo.v >"$WD"/ref.v
${LHD} lec --set formal.effort=proof --impl verilog:"$WD"/all.v --ref verilog:"$WD"/ref.v --top foo \
  --workdir "$WD"/wc -q >"$WD"/check.log 2>&1 || {
  echo "FAIL(filelist): LEC mismatch vs source"
  tail -10 "$WD"/check.log
  exit 1
}

# Bazel canonical repository directories contain literal plus signs. Both
# nested command files and raw arguments must retain those directory names.
SPECIAL="$PWD/$WD/space dir/+_repo_rules+cells"
mkdir -p "$SPECIAL" "$WD/inc_a" "$WD/inc_b"
printf "%s\n" "\`define VALUE_A 8'h35" > "$SPECIAL/value.svh"
printf "%s\n" "\`define VALUE_B 8'hCA" > "$WD/inc_b/other.svh"
cat > "$WD/plus.sv" <<'SV'
`include "value.svh"
`include "other.svh"
module plus(output [7:0] y);
assign y = `VALUE_A ^ `VALUE_B;
endmodule
SV
printf '"+incdir+%s"\n+incdir+%s+%s\n%s\n' "$SPECIAL" "$PWD/$WD/inc_a" "$PWD/$WD/inc_b" "$PWD/$WD/plus.sv" > "$WD/inner.f"
printf -- '-f "%s"\n' "$PWD/$WD/inner.f" > "$WD/outer.f"
${LHD} compile --top plus --workdir "$WD/plus_work" --emit verilog:"$WD/plus.v" \
  -- -f "$WD/outer.f" > "$WD/plus.log" 2>&1 || { cat "$WD/plus.log"; exit 1; }
cat > "$WD/plus_gold.v" <<'SV'
module plus(output [7:0] y); assign y = 8'hff; endmodule
SV
${LHD} lec --impl "$WD/plus.v" --ref "$WD/plus_gold.v" --top plus \
  --set formal.effort=proof --workdir "$WD/plus_lec" -q > "$WD/plus_lec.log" 2>&1 \
  || { cat "$WD/plus_lec.log"; exit 1; }
echo "PASS(filelist)"
exit 0
