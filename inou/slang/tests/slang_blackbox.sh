#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# Unknown-module (blackbox) policy of --reader slang:
#   1. WITHOUT a user --ignore-unknown-modules the compile must FAIL with the
#      clean located `unknown-module` error (a typo'd module name never passes
#      silently).
#   2. WITH the flag, the unknown instances lower as blackbox sub-instances
#      (port directions inferred) and the emitted pyrope imports them.
#   3. Supplying hand-written .prp definitions for the blackboxes next to the
#      emitted pyrope must recompile AND LEC-prove against the golden verilog
#      (bbtop.v + bbdefs.v), single- and multi-output blackboxes both.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x $LHD ]; then
  if [ -x ./lhd/lhd ]; then
    LHD=./lhd/lhd
  else
    echo "FAILED: slang_blackbox.sh could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

DIR=inou/slang/tests/blackbox
WD=tmp_slang/blackbox
rm -rf "$WD"
mkdir -p "$WD"

# 1. No user flag: clean unknown-module error, non-zero exit.
if ${LHD} compile --top bbtop --emit-dir pyrope:"$WD"/prp_nof/ --workdir "$WD"/w0 -q \
    -- "$DIR"/bbtop.v >"$WD"/nof.log 2>&1; then
  echo "FAIL(blackbox): compile without --ignore-unknown-modules must fail"
  exit 1
fi
grep -q "refers to an unknown module" "$WD"/nof.log || {
  echo "FAIL(blackbox): expected the located unknown-module error"
  cat "$WD"/nof.log
  exit 1
}

# 2. User flag: blackbox instances, pyrope emission with imports.
${LHD} compile --top bbtop --emit-dir pyrope:"$WD"/prp/ --workdir "$WD"/w1 -q \
  -- --ignore-unknown-modules "$DIR"/bbtop.v >"$WD"/bb.log 2>&1 || {
  echo "FAIL(blackbox): compile with --ignore-unknown-modules failed"
  cat "$WD"/bb.log
  exit 1
}
for m in array_0_ext bb2; do
  grep -q "import(\"$m.$m\")" "$WD"/prp/bbtop.prp || {
    echo "FAIL(blackbox): emitted bbtop.prp does not import blackbox '$m'"
    cat "$WD"/prp/bbtop.prp
    exit 1
  }
done

# 3. Supply the blackbox definitions as .prp and LEC against the golden.
cp "$DIR"/array_0_ext.prp "$DIR"/bb2.prp "$WD"/prp/
cat "$DIR"/bbtop.v "$DIR"/bbdefs.v >"$WD"/golden.v
${LHD} lec --impl pyrope:"$WD"/prp --ref verilog:"$WD"/golden.v --top bbtop \
  --workdir "$WD"/wc -q >"$WD"/lec.log 2>&1 || {
  echo "FAIL(blackbox): LEC vs golden failed"
  tail -10 "$WD"/lec.log
  exit 1
}

echo "PASS(blackbox)"
exit 0
