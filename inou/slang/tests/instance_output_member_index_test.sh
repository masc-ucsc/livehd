#!/bin/bash
# An instance output read ONLY as the index of an array element whose FIELD is
# accessed (`data_i[bin].tag`) must still register as a READ of that net.
#
# Dep_collector's MemberAccess handler canonicalizes a member read to its root
# symbol, but defining handle() REPLACES slang's default traversal — so
# returning without descending discarded the whole sub-expression, and with it
# the read nested in the element SELECTOR. `way_bin` then had no reader at all:
# the back-edge wire classification never saw the early read, the net kept its
# `0sb?` poison initializer, and its instance-output binding — emitted after
# every reader — was dropped as dead. Every consumer read X forever.
#
# This is CVA6 miss_handler's `data_i[lfsr_bin].tag/.data/.dirty` against
# lfsr_8bit's `refill_way_bin` (3 pass.abc `unknown-shift-amount` refusals).
# `refill_way_oh`, read as a plain value, was unaffected — that asymmetry is
# what made it visible, so `way_oh` is asserted here as the control.
#
# Asserted on the EMITTED PYROPE on purpose: lgyosys answers INCONCLUSIVE on
# this shape, and an inconclusive lgcheck does not fail a pair, so an
# equivalence harness would pass while the binding was missing.

set -eu

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  LHD=./lhd/lhd
fi

src=$1
wd=tmp_slang/instance_output_member_index
rm -rf "$wd"
mkdir -p "$wd"

if ! "$LHD" compile "$src" --reader slang --top instance_output_as_member_index \
  --emit-dir pyrope:"$wd"/prp --workdir "$wd"/w >"$wd"/compile.log 2>&1; then
  echo "FAIL: --reader slang could not lower $src"
  cat "$wd"/compile.log
  exit 1
fi

# The TOP module's file by name, not `ls | head -1`: the callee is emitted
# beside it and its own `way_bin_o`/`way_oh_o` PORT declarations would satisfy
# the connection checks below without the top ever binding the instance.
prp="$wd"/prp/instance_output_as_member_index.prp
if [ ! -f "$prp" ]; then
  echo "FAIL: no pyrope emitted for the top module"
  ls -l "$wd"/prp 2>&1
  exit 1
fi

# The bug: the net keeps a poison initializer because nothing was seen to read it.
if grep -qE '^[[:space:]]*way_bin[[:space:]]*=[[:space:]]*0[su]b\?' "$prp"; then
  echo "FAIL: way_bin kept its 0sb? poison — the member-access index read was not recorded"
  cat "$prp"
  exit 1
fi

# The consequence: the instance-output binding is emitted after every reader and
# dropped as dead. Either a direct binding or an inlined `i_lfsr.way_bin_o` read
# is fine; what must never happen is neither.
#
# Require the name in a USE position -- a dotted instance read
# (`i_lfsr.way_bin_o`, what the inliner emits) or the named-connection binding
# (`way_bin_o = ...`) -- so a stray mention in a comment or a port list cannot
# satisfy the check. The trailing boundary must accept END OF LINE: the dotted
# read is the last token of its statement.
binding_re() { grep -qE "(\\.$1([^A-Za-z0-9_]|\$)|[^A-Za-z0-9_.]$1[[:space:]]*=)" "$prp"; }
if ! binding_re way_bin_o; then
  echo "FAIL: the way_bin_o instance-output connection is missing entirely"
  cat "$prp"
  exit 1
fi

# Control: the plain-value output was never affected and must stay connected.
if ! binding_re way_oh_o; then
  echo "FAIL: the way_oh_o control connection is missing"
  cat "$prp"
  exit 1
fi

echo "PASS: an instance output used only as a member-access index stays connected"
