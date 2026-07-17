#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# 2f-lec diverged-use memory collapse guard. Memories collapse across the two
# designs by SHAPE (size x bits) x RTL occurrence order — memory hier-names are
# positional node-ids, so occurrence is the only pairing. That is a soundness
# hazard when a shape bucket holds >1 memory per side and the two designs order
# them differently: the wrong two memories then share one current-state array,
# which can MASK a real difference (false PROVEN). The guard leaves a bucket
# UNCOLLAPSED whenever semdiff cannot confirm the occurrence pairing (or flags a
# genuine divergence), so such a design can never come back a false PROVEN — the
# verdict degrades to REFUTED/INCONCLUSIVE (sound). The guard must NOT regress a
# legitimate design: a same-shape-memory design proved against itself, where the
# occurrence pairing IS correct, must still PROVE.

set -u

LHD=./bazel-bin/lhd/lhd
if [ ! -x "$LHD" ]; then
  if [ -x ./lhd/lhd ]; then LHD=./lhd/lhd; else
    echo "FAIL: could not find the lhd binary in $(pwd)"; exit 1; fi
fi

WORK="${TEST_TMPDIR:-/tmp/lecmemdiverge}"
mkdir -p "$WORK"
fail=0

# Two same-shape (4x8) writable memories. m0 and m1 receive DISTINCT writes
# (wd vs wd^0xFF), so they are genuinely different arrays; the output reads
# m0[ra0] ^ m1[ra1].
cat > "$WORK/ref.v" <<'EOF'
module foo(input clk, input we, input [1:0] wa, input [7:0] wd,
           input [1:0] ra0, input [1:0] ra1, output [7:0] z);
  reg [7:0] m0 [0:3];
  reg [7:0] m1 [0:3];
  always @(posedge clk) begin
    if (we) begin m0[wa] <= wd; m1[wa] <= wd ^ 8'hFF; end
  end
  assign z = m0[ra0] ^ m1[ra1];
endmodule
EOF

# HAZARD: the two memories are declared in the OPPOSITE order (m1 then m0) and
# the output reads them crossed (m1[ra0] ^ m0[ra1]). This is a genuinely
# DIFFERENT circuit, but a crossed shape+occurrence collapse (ref.m0 <-> impl.m1)
# would make it look equivalent -> a false PROVEN. The guard must prevent that.
cat > "$WORK/crossed.v" <<'EOF'
module foo(input clk, input we, input [1:0] wa, input [7:0] wd,
           input [1:0] ra0, input [1:0] ra1, output [7:0] z);
  reg [7:0] m1 [0:3];
  reg [7:0] m0 [0:3];
  always @(posedge clk) begin
    if (we) begin m0[wa] <= wd; m1[wa] <= wd ^ 8'hFF; end
  end
  assign z = m1[ra0] ^ m0[ra1];
endmodule
EOF

verdict() {  # $1=ref $2=impl $3=decompose -> PROVEN | REFUTED | UNKNOWN | INCONCLUSIVE
  $LHD lec --ref "$WORK/$1" --impl "$WORK/$2" --top foo \
       --set lec.hier=false --set lec.engine=ind --set lec.decompose="$3" \
       --workdir "$WORK/q_${1}_${2}_${3}_$$" 2>&1 \
    | grep -o "PROVEN equivalent\|REFUTED (not equivalent)\|UNKNOWN\|INCONCLUSIVE" | head -1
}

# 1) No regression: a 2-same-shape-memory design proved against ITSELF (occurrence
#    pairing is correct) must still PROVE — the guard must not block a legitimate
#    same-shape collapse.
for dec in true false; do
  v=$(verdict ref.v ref.v "$dec")
  if [ "$v" = "PROVEN equivalent" ]; then
    echo "ok: self-LEC (decompose=$dec) -> PROVEN"
  else
    echo "FAIL: self-LEC (decompose=$dec) -> '$v' (want PROVEN; guard over-blocked a legitimate collapse)"; fail=1
  fi
done

# 2) Soundness: the crossed (genuinely different) design must NEVER come back
#    PROVEN. REFUTED or an inconclusive (UNKNOWN/INCONCLUSIVE) degrade are both
#    sound; only a PROVEN is a wrong verdict.
for dec in true false; do
  v=$(verdict ref.v crossed.v "$dec")
  if [ "$v" = "PROVEN equivalent" ]; then
    echo "FAIL: crossed design (decompose=$dec) -> FALSE PROVEN (a wrong verdict)"; fail=1
  elif [ -n "$v" ]; then
    echo "ok: crossed design (decompose=$dec) -> $v (sound, not a false PROVEN)"
  else
    echo "FAIL: crossed design (decompose=$dec) -> no verdict parsed"; fail=1
  fi
done

if [ $fail -ne 0 ]; then echo "lec_mem_diverge_test: FAILED"; exit 1; fi
echo "lec_mem_diverge_test: PASSED"
exit 0
