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
${LHD} check --impl verilog:"$WD"/all.v --ref verilog:"$WD"/ref.v --top foo \
  --workdir "$WD"/wc -q >"$WD"/check.log 2>&1 || {
  echo "FAIL(filelist): LEC mismatch vs source"
  tail -10 "$WD"/check.log
  exit 1
}

echo "PASS(filelist)"
exit 0
