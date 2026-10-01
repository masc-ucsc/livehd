#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--emit-dir pyrope:DIR` re-emission over a construct-rich source. The
# pyrope: slot turns the coalescer upass on (toln consumers exist), so the
# reduction/popcount/tuple-concat/while/is per-op hooks all run, then
# pass.prp_writer re-emits the units. Checks: the verifier discharges every
# cassert, the emitted top unit holds the expected statements, and a unit
# file exists per lambda.

set -u

LHD=lhd/lhd
PRP=lhd/tests/writer_rich.prp
W="${TEST_TMPDIR:-/tmp/lhd_prp_writer_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

"$LHD" compile "$PRP" --emit-dir pyrope:"$W/out/" \
   --set upass.verifier_pass=7 --set upass.verifier_fail=0 \
  --workdir "$W/w" -q 2>/dev/null \
  || fail "compile with pyrope: emission failed (or verifier count mismatch)"

[ -f "$W/out/manifest.json" ] || fail "pyrope: emission produced no manifest"

TOP="$W/out/writer_rich.prp"
[ -s "$TOP" ] || fail "missing emitted top unit writer_rich.prp"
# The while loop unrolls to its final iteration values.
grep -q 'w = 20' "$TOP" || fail "emitted top unit lost the unrolled while-loop writes"
# Tuple-concat scaffolding survives to the emitted source.
grep -q '(3, 4)' "$TOP" || fail "emitted top unit lost the tuple literal"

# One .prp per SOURCE FILE: both lambdas of writer_rich.prp land in the one
# emitted writer_rich.prp (below the file-scope statements), and a same-file
# callee takes no import.
grep -q '^pub comb helper(' "$TOP" || fail "missing emitted lambda comb helper"
grep -q '^pub comb rich(' "$TOP" || fail "missing emitted lambda comb rich"
[ -f "$W/out/writer_rich.rich.prp" ] && fail "per-lambda file writer_rich.rich.prp came back"
grep -q 'import("writer_rich' "$TOP" && fail "same-file callee must not be imported"

# A comb INPUT default (`b:U4 = 3`, todo 3g E) lives in the body prologue as a
# `__default_b` store that nothing in the unit's own body reads, so the writer
# dropped it and re-emitted `b:U4`: a re-parsed caller that omits the argument
# then no longer compiled. The default goes back into the signature.
cat >"$W/dflt.prp" <<'EOF'
comb f(a:U4, b:U4 = 3) -> (y:U4) {
  y = a ^ b
}
comb g(a:U4, b:U4 = a ^ 5, c:U4 = 1) -> (y:U4) {
  y = a ^ b ^ c
}
comb h(a:U4, b:U4 = a#[0..<2]) -> (y:U4) {
  y = a ^ b
}
EOF
"$LHD" compile "$W/dflt.prp" --emit-dir pyrope:"$W/dout/" --workdir "$W/wd" -q 2>/dev/null \
  || fail "compile with pyrope: emission of defaulted combs failed"
grep -q '^pub comb f(a:U4, b:U4 = 3) ' "$W/dout/dflt.prp" || fail "constant default lost: $(cat "$W/dout/dflt.prp")"
grep -q '^pub comb g(a:U4, b:U4 = a ^ 5, c:U4 = 1) ' "$W/dout/dflt.prp" \
  || fail "expression default lost: $(cat "$W/dout/dflt.prp")"
grep -q '^pub comb h(a:U4, b:U4 = a#\[0\.\.=1\]) ' "$W/dout/dflt.prp" \
  || fail "bit-select default lost: $(cat "$W/dout/dflt.prp")"
# The re-emitted defaults are live: calls that omit the arguments recompile and
# compute the defaulted values.
{
  cat "$W/dout/dflt.prp"
  echo 'pub mod dtop(a:U4) -> (y:U4@[0], z:U4@[0], w:U4@[0]) {'
  echo '  y = f(a=a)'
  echo '  z = g(a=a)'
  echo '  w = h(a=a)'
  echo '}'
} >"$W/dtop.prp"
cat >"$W/dtop.v" <<'EOF'
module dtop(input [3:0] a, output [3:0] y, output [3:0] z, output [3:0] w);
  assign y = a ^ 4'd3;
  assign z = a ^ (a ^ 4'd5) ^ 4'd1;
  assign w = a ^ {2'b00, a[1:0]};
endmodule
EOF
"$LHD" lec --impl "$W/dtop.prp" --ref "$W/dtop.v" --top dtop --set formal.timeout=60 --workdir "$W/wl" -q \
  >"$W/lec.json" 2>/dev/null || fail "re-emitted defaults are not equivalent: $(cat "$W/lec.json")"

echo "PASS lhd_prp_writer_test"
