#!/usr/bin/env bash
# 2f-lgimport — the lg: import (compiled lgraph) two-step flow + LEC, plus the
# not-found and port-mismatch diagnostics and an lg: unit from each producer
# (slang Verilog and Pyrope). Self-contained: generates its own fixtures.
set -u

if   [ -x ./bazel-bin/lhd/lhd ]; then LHD=./bazel-bin/lhd/lhd
elif [ -x ./lhd/lhd ];           then LHD=./lhd/lhd
else echo "FAIL: lhd binary not found"; exit 3; fi

W=$(mktemp -d)
trap 'rm -rf "$W"' EXIT
fail() { echo "FAIL: $*"; exit 1; }

# ---- fixtures ---------------------------------------------------------------
cat > "$W/addsub.v" <<'EOF'
module addsub(input [7:0] a, input [7:0] b,
              output [8:0] add, output [8:0] sub);
  assign add = a + b;   // depends on (a,b)
  assign sub = a - b;
endmodule
EOF
cat > "$W/lg_use.prp" <<'EOF'
const addsub = import("lg:addsub")
mod lg_use_top(a:u8, b:u8) -> (o:u9@[0]) {
  o = addsub(a=a, b=b).add
}
EOF
cat > "$W/gold.v" <<'EOF'
module \lg_use.lg_use_top (input [7:0] a, input [7:0] b, output [8:0] o);
  assign o = a + b;
endmodule
EOF
cat > "$W/notfound.prp" <<'EOF'
const nope = import("lg:does_not_exist")
mod nf_top(a:u8) -> (o:u8@[0]) { o = nope(a=a).z }
EOF
cat > "$W/badport.prp" <<'EOF'
const addsub = import("lg:addsub")
mod bp_top(a:u8, b:u8) -> (o:u9@[0]) { o = addsub(a=a, zzz=b).add }
EOF
cat > "$W/plib.prp" <<'EOF'
pub mod prp_add::[lg="prp_add"](a:u8, b:u8) -> (s:u9@[0]) { s = a + b }
EOF
cat > "$W/prp_use.prp" <<'EOF'
const prp_add = import("lg:prp_add")
mod prp_use_top(a:u8, b:u8) -> (o:u9@[0]) { o = prp_add(a=a, b=b).s }
EOF

# ---- 1. slang producer: compile add_sub.v into an lgdb ----------------------
$LHD compile "$W/addsub.v" --reader slang --top addsub --emit-dir "lg:$W/lgdb/" \
  --workdir "$W/w1" -q || fail "step 1 (slang -> lg) did not compile"

# ---- 2. import the lg: unit from a Pyrope source; check the instance --------
$LHD compile "$W/lg_use.prp" "lg:$W/lgdb/" --emit-dir "verilog:$W/out/" \
  --workdir "$W/w2" -q || fail "step 2 (import lg:addsub) did not compile"
grep -q "addsub u_" "$W/out/lg_use.lg_use_top.v" || fail "top does not instantiate the imported addsub"

# ---- 3. LEC: top (instantiating addsub) + the addsub body == flat a+b -------
cat "$W/out/lg_use.lg_use_top.v" "$W/addsub.v" > "$W/impl.v"
$LHD check --impl "verilog:$W/impl.v" --ref "verilog:$W/gold.v" \
  --impl-top lg_use.lg_use_top --ref-top lg_use.lg_use_top --workdir "$W/wlec" -q \
  || fail "LEC: imported lg netlist is not equivalent to a+b"

# ---- 4. not-found diagnostic -----------------------------------------------
if $LHD compile "$W/notfound.prp" "lg:$W/lgdb/" --emit-dir "verilog:$W/nf/" \
     --workdir "$W/w4" -q >"$W/nf.err" 2>&1; then
  fail "importing a missing lg: unit should error"
fi
grep -qi "not found" "$W/nf.err" || fail "not-found error lacks a clear message"

# ---- 5. port-mismatch diagnostic (must be a clean error, not an abort) ------
if $LHD compile "$W/badport.prp" "lg:$W/lgdb/" --emit-dir "verilog:$W/bp/" \
     --workdir "$W/w5" -q >"$W/bp.err" 2>&1; then
  fail "calling an imported module with a wrong port should error"
fi
grep -qi "Assertion" "$W/bp.err" && fail "port mismatch aborted instead of a clean diagnostic"
grep -qi "does not have\|no input named" "$W/bp.err" || fail "port-mismatch error lacks a clear message"

# ---- 6. lg: unit from a Pyrope producer (::[lg=...] pinned name) ------------
$LHD compile "$W/plib.prp" --top prp_add --emit-dir "lg:$W/plgdb/" \
  --workdir "$W/w6a" -q || fail "pyrope producer (plib) did not compile to lg"
$LHD compile "$W/prp_use.prp" "lg:$W/plgdb/" --emit-dir "verilog:$W/pout/" \
  --workdir "$W/w6b" -q || fail "import of a pyrope-produced lg unit did not compile"
grep -q "prp_add u_" "$W/pout/prp_use.prp_use_top.v" || fail "top does not instantiate the pyrope-produced prp_add"

echo "lg_import_test - PASS (slang+pyrope two-step LEC, not-found, port-mismatch)"
exit 0
