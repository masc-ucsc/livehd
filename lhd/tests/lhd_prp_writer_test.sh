#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# `--emit-dir pyrope:DIR` re-emission over a construct-rich source. The
# pyrope: slot turns the coalescer upass on (toln consumers exist), so the
# reduction/popcount/tuple-concat/while/is per-op hooks all run, then
# pass.prp_writer re-emits the units. Checks: the verifier discharges every
# cassert, the emitted top unit holds the expected statements, and a unit
# file exists per lambda.
#
# One section per bazel target keeps each under the test time budget:
#   emit       writer_rich, input defaults, Verilog clock classification
#   roundtrip_a / roundtrip_b
#              emit -> fmt -> unbounded LEC of half the regression fixtures each
#   cgen       Pyrope -> Verilog LEC, lgcheck readmem proofs, bad lec matches
# No argument runs every section.

set -u

SECTION="${1:-all}"
section() { [ "$SECTION" = all ] || [ "$SECTION" = "$1" ]; }

LHD=lhd/lhd
PRP=lhd/tests/writer_rich.prp
W="${TEST_TMPDIR:-/tmp/lhd_prp_writer_$$}"
mkdir -p "$W"

fail() {
  echo "FAIL: $*" >&2
  exit 1
}

if section emit; then
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
grep -q '^pub comb h(a:U4, b:U4 = a#\[0\.\.<2\]) ' "$W/dout/dflt.prp" \
  || fail "bit-select default lost: $(cat "$W/dout/dflt.prp")"
# The select temp is read only by the default (its `type_spec` window reads
# nothing): no dead body line duplicates it.
grep -q 'const t[0-9_]*:U2 = a#' "$W/dout/dflt.prp" \
  && fail "bit-select default left a dead body temp: $(cat "$W/dout/dflt.prp")"
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

# A Verilog clock that is ALSO data has no Pyrope spelling (a Clock is not
# data, a U1 cannot clock): the writer reports it instead of writing a unit
# whose recompile fails clock-bind-not-clock. Locally (`assign s = clk`), and
# through an instance (the parent's own register on a clock the child reads).
mkdir -p "$W/clk"
cat >"$W/clk/clock_as_data.sv" <<'EOF'
module top(input logic clk, input logic [3:0] d, output logic [3:0] q, output logic s);
  always_ff @(posedge clk) q <= d;
  assign s = clk;
endmodule
EOF
cat >"$W/clk/clock_as_data_hier.sv" <<'EOF'
module leaf(input logic c, input logic d, output logic s); assign s = c & d; endmodule
module top(input logic clk, input logic d, output logic q, output logic s);
  always_ff @(posedge clk) q <= d;
  leaf u(.c(clk), .d(d), .s(s));
endmodule
EOF
for fixture in clock_as_data clock_as_data_hier; do
  if "$LHD" compile "$W/clk/$fixture.sv" --top top --emit-dir pyrope:"$W/clk/$fixture" \
      --emit diagnostics:"$W/clk/$fixture.jsonl" --workdir "$W/clk/w_$fixture" -q >/dev/null 2>&1; then
    fail "$fixture: a clock read as data was emitted: $(cat "$W/clk/$fixture/top.prp")"
  fi
  grep -q '"code":"prp-writer-clock-as-data".*input `clk` clocks a register' "$W/clk/$fixture.jsonl" \
    || fail "$fixture: missing prp-writer-clock-as-data: $(cat "$W/clk/$fixture.jsonl")"
done
# The clock classification is an overlay, never written into the units: a
# second emit in the same run sees the same io_meta and writes the same files
# (a promoted `x` used to be re-read as a data-read Clock and demote `child.c`).
cat >"$W/clk/twice.sv" <<'EOF'
module child(input c, input d, output reg q); always @(posedge c) q <= d; endmodule
module top(input x, input d, output y, output q); assign y = x & d; child u(.c(x), .d(d), .q(q)); endmodule
EOF
"$LHD" compile "$W/clk/twice.sv" --top top --emit-dir pyrope:"$W/clk/once" --emit-dir pyrope:"$W/clk/again" \
  --set prp_writer.debug=true --workdir "$W/clk/w_twice" -q >/dev/null 2>&1 || fail "twice.sv emission"
diff -r "$W/clk/once" "$W/clk/again" >"$W/clk/twice.diff" || fail "second emit differs: $(cat "$W/clk/twice.diff")"
fi


# Regressions: validate emitted syntax, formatting, and circuit behavior.
mkdir -p "$W/regress/generic"
cat >"$W/regress/emit_bool.prp" <<'EOF'
pub comb top(a:U2) -> (out:Bool) {
  mut seen:Bool = false
  for bit in 0..<2 { seen = seen or a#[bit] != 0 }
  out = seen
}
EOF
cat >"$W/regress/emit_output_loop.prp" <<'EOF'
pub comb top(a:U3) -> (out:U12) {
  for lane in 0..<4 { out#[(lane*3)..+3] = a }
}
EOF
cat >"$W/regress/emit_mask.prp" <<'EOF'
pub comb top(a:U4) -> (out:U8) {
  mut remaining:U4 = a
  for slot in 0..<2 {
    const winner:U4 = remaining & -remaining
    out#[(slot*4)..+4] = winner
    remaining ^= winner
  }
}
EOF
cat >"$W/regress/emit_wire_slice.sv" <<'EOF'
module leaf #(parameter Width=2)(input logic [Width-1:0] d, output logic [Width-1:0] q);
  assign q = d;
endmodule
module top(input logic [1:0] d, output logic q);
  logic [0:0][0:0] bus;
  logic ignored;
  leaf u(.d(d), .q({bus[0], ignored}));
  assign q = bus[0];
endmodule
EOF
cat >"$W/regress/typed_cast.prp" <<'EOF'
// A sized cast of this scalar tuple-field expression should compile.
pub comb top(select:(e1:U1, e0:U1)) -> (out:U2) {
  out = ~U2((select.e1 << 1) | select.e0)
}
EOF
cat >"$W/regress/generic/core.prp" <<'EOF'
pub comb core<n=2>(a:Unsigned(bits=n))->(out:Unsigned(bits=n)) { out=a }
EOF
cat >"$W/regress/generic/top.prp" <<'EOF'
const core_fn=import("core.core")
pub mod top<n=4>(a:Unsigned(bits=n))->(out:Unsigned(bits=n)@[0]) { out=core_fn<n=n>(a) }
EOF
cat >"$W/regress/forward_clock.sv" <<'EOF'
module leaf(input logic clk, input logic d, output logic q); always_ff @(posedge clk) q <= d; endmodule
module top(input logic source_tick, input logic d, output logic q); leaf u(.clk(source_tick), .d(d), .q(q)); endmodule
EOF
cat >"$W/regress/inline_loop.prp" <<'EOF'
comb mask<n=4>(a:Unsigned(bits=n))->(out:Unsigned(bits=n)) {
 out=0
 for lane in 0..<n {out#[lane]=a#[lane]}
}
pub comb top(a:U4)->(out:U4) {out=mask<n=4>(a)}
EOF
cat >"$W/regress/loop_scope.prp" <<'EOF'
pub mod top(a:U4, en:Bool) -> (out:U16@[0]) {
  mut matrix:U16=0
  if en {
    for i in 0..<4 { matrix#[(i*4)..+4] = if a#[i] == 1 {15} else {0} }
  }
  for i in 0..<4 {
    for j in 0..<4 { matrix#[(i*4)+j] = a#[j] }
  }
  out=matrix
}
EOF
cat >"$W/regress/array_partial.prp" <<'EOF'
pub comb top()->(out:U2) {
  mut lanes:[1]U2=0
  lanes=0sb?
  lanes[0]#[0]=1
  lanes[0]#[1]=1
  out=lanes[0]
}
EOF
cat >"$W/regress/helper.prp" <<'EOF'
comb limit(size)->(out) {out=(1<<size)-1}
comb identity<n=4>(a:Unsigned(bits=n))->(out:Unsigned(bits=n)) {
  comptime const MASK=limit(n)
  out=a & MASK
}
pub comb top(a:U4)->(out:U4) {out=identity<n=4>(a)}
EOF
cat >"$W/regress/clock_wrappers.sv" <<'EOF'
module leaf(input logic clk, d, output logic q); always_ff @(posedge clk) q<=d; endmodule
module unused_leaf(input logic clk); endmodule
module mid #(parameter N=1)(input logic src_clk, dst_clk, d, output logic q);
  leaf u(.clk(dst_clk), .d(d), .q(q));
  unused_leaf dead(.clk(src_clk));
endmodule
module top(input logic push_clk, pop_clk, a, b, output logic x,y);
  mid p(.src_clk(pop_clk), .dst_clk(push_clk), .d(a), .q(x));
  mid n(.src_clk(push_clk), .dst_clk(pop_clk), .d(b), .q(y));
endmodule
EOF
# A Verilog clock gate is the Clock_cell `Clock(clock_pin=clk, enable=...)`,
# not a data read of `clk`: the port stays a Clock.
cat >"$W/regress/emit_gated_clock.sv" <<'EOF'
module top(input logic clk, input logic en, input logic [3:0] d, output logic [3:0] q, output logic [3:0] r);
  logic en_q;
  always_ff @(posedge clk) en_q <= en;
  wire gclk = clk & en & ~en_q;
  always_ff @(posedge gclk) r <= d;
  always_ff @(posedge clk) q <= d;
endmodule
EOF
printf '12\n34\n' >"$W/regress/image.hex"
cat >"$W/regress/readmem.sv" <<EOF
module top(input logic clock, addr, output logic [7:0] out);
  logic [7:0] words[0:1];
  initial \$readmemh("$W/regress/image.hex", words);
  assign out=words[addr];
endmodule
EOF
# The LEC compiles the emitted (and reformatted) unit, so it is also the
# recompile check.
roundtrip=()
section roundtrip_a && roundtrip+=(emit_bool.prp emit_output_loop.prp emit_mask.prp emit_wire_slice.sv typed_cast.prp
  generic/top.prp forward_clock.sv)
section roundtrip_b && roundtrip+=(inline_loop.prp loop_scope.prp array_partial.prp helper.prp clock_wrappers.sv readmem.sv
  emit_gated_clock.sv)
for fixture in ${roundtrip[@]+"${roundtrip[@]}"}; do
  name="${fixture%.*}"
  dir="$W/regress/out/$name"
  "$LHD" compile "$W/regress/$fixture" --top top --emit-dir pyrope:"$dir" --workdir "$dir/work" -q \
    >"$W/regress/emit.json" 2>"$W/regress/error" || fail "$fixture emission: $(cat "$W/regress/error")"
  if [[ "$fixture" == *.sv ]]; then emitted="$dir/top.prp"; else emitted="$dir/$(basename "$fixture")"; fi
  "$LHD" pyrope fmt -i "$dir"/*.prp >"$W/regress/fmt.log" 2>&1 || fail "$fixture formatting"
  "$LHD" lec --ref "$W/regress/$fixture" --impl "$emitted" --top top --set formal.timeout=5 \
    --workdir "$dir/lec" --result-json "$dir/lec.json" -q >"$W/regress/lec.log" 2>&1 \
    || fail "$fixture roundtrip equivalence: $(cat "$W/regress/lec.log")"
  python3 - "$dir/lec.json" <<'PYJSON'
import json, sys
r = json.load(open(sys.argv[1]))
assert r["lec"]["verdict"] == "proven" and not r["lec"].get("bounded", False), r
PYJSON
  [ "$?" -eq 0 ] || fail "$fixture did not prove unbounded equivalence"
done
if section roundtrip_b; then
  grep -q 'clk:Clock' "$W/regress/out/emit_gated_clock/top.prp" \
    && grep -q 'gclk = Clock(clock_pin=clk, enable=' "$W/regress/out/emit_gated_clock/top.prp" \
    || fail "clock gate not spelled as a Clock_cell: $(cat "$W/regress/out/emit_gated_clock/top.prp")"
fi

if section cgen; then

# Recompiling the Verilog emitted from Pyrope must preserve arithmetic widths
# inside comparisons and preserve elaboration-time memory preload selection.
cat >"$W/regress/compare.prp" <<'EOF'
pub comb top(a:U4,b:U4,c:U4,d:U4)->(gt:Bool,lt:Bool,eq:Bool) {
  gt=(a+b)>(c*d)
  lt=(a+b)<(c*d)
  eq=(a+b)==(c*d)
}
EOF
for fixture in compare.prp readmem.sv; do
  dir="$W/regress/cgen/${fixture%.*}"
  mkdir -p "$dir"
  "$LHD" compile "$W/regress/$fixture" --top top --emit verilog:"$dir/net.v" --workdir "$dir/work" -q \
    >"$dir/compile.json" 2>"$dir/error" || fail "$fixture Verilog emission"
  "$LHD" lec --ref "$W/regress/$fixture" --impl "$dir/net.v" --top top --set formal.timeout=5 \
    --workdir "$dir/lec" --result-json "$dir/lec.json" -q >"$dir/lec.log" 2>&1 \
    || fail "$fixture emitted-Verilog equivalence: $(cat "$dir/lec.log")"
  python3 - "$dir/lec.json" <<'PYJSON'
import json, sys
r = json.load(open(sys.argv[1]))
assert r["lec"]["verdict"] == "proven" and not r["lec"].get("bounded", False), r
PYJSON
  [ "$?" -eq 0 ] || fail "$fixture emitted Verilog did not prove"
done

# Independent readers must see file-backed memory behavior rather than a black box.
LGCHECK="$PWD/inou/yosys/lgcheck"
YOSYS_ABS="$PWD/inou/yosys/yosys2"
sed 's/out=words/out=~words/' "$W/regress/readmem.sv" >"$W/regress/readmem_bad.sv"
for reader in verilog slang; do
  dir="$W/regress/cgen/readmem/yosys-$reader"
  mkdir -p "$dir"
  (cd "$dir" && LGCHECK_EQUIV_TIMEOUT=5 "$LGCHECK" --yosys "$YOSYS_ABS" \
    --gold_reader "$reader" --gate_reader "$reader" --top top \
    --reference "$W/regress/readmem.sv" --implementation "$W/regress/cgen/readmem/net.v") \
    >"$dir/proof.log" 2>&1 || fail "file memory $reader proof: $(cat "$dir/proof.log")"
  (cd "$dir" && LGCHECK_EQUIV_TIMEOUT=5 "$LGCHECK" --yosys "$YOSYS_ABS" \
    --gold_reader "$reader" --gate_reader "$reader" --top top \
    --reference "$W/regress/readmem_bad.sv" --implementation "$W/regress/cgen/readmem/net.v") \
    >"$dir/refute.log" 2>&1
  [ "$?" -eq 1 ] || fail "file memory $reader did not refute changed output: $(cat "$dir/refute.log")"
done

# Bad side qualifiers must fail before a solver consumes the timeout budget.
if "$LHD" lec --ref "$W/regress/forward_clock.sv" --impl "$W/regress/forward_clock.sv" --top top \
    --set formal.lec.match=ref.u.q=impl.u.q --workdir "$W/regress/badmatch" \
    --result-json "$W/regress/badmatch.json" -q >"$W/regress/badmatch.log" 2>&1; then
  fail "unknown explicit correspondence names were accepted"
fi
python3 - "$W/regress/badmatch.json" <<'PYJSON'
import json, sys
r = json.load(open(sys.argv[1]))
assert r["error"]["class"] == "usage", r
assert "unresolved reference state" in r["error"]["message"], r
assert "unresolved implementation state" in r["error"]["message"], r
PYJSON
[ "$?" -eq 0 ] || fail "missing structured unresolved-match diagnostic"
fi

echo "PASS lhd_prp_writer_test $SECTION"
