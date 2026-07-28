#!/bin/bash
# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
# A packed-struct field access must lower the SAME WAY at every nesting depth.
#
# A qualifying packed-struct PORT is "bundled": flattened into per-field dotted
# leaf nets (`port.field`, body base `port__bpo.field`) that lower to plain
# scalar stores, so each field is an INDEPENDENT net. `struct_port_bundle_ok`
# used to disqualify the whole port as soon as ONE field was itself a struct
# ("nested struct / union field -- deferred, keep the flat port"), so `a.b.c`
# fell back to a packed WORD: read = get_mask(whole sub-struct) + sra + and,
# write = set_mask on the word.
#
# That is not merely slower, it is WRONG-SHAPED. Writing field X and reading a
# DISJOINT field Y of the same struct becomes a self-reference on one word, and
# a design that is perfectly acyclic field-by-field acquires a FALSE
# combinational cycle. In lhdsuite's minion this is `vpu_tensorfma.sv`:
#
#   :425  dcache_ctrl.scp_req.bid = ... : dc_read_req_a;
#   :436  dc_read_req_a = (dcache_ctrl.scp_req.read_en || ...) && ...;
#
# bid <- dc_read_req_a <- read_en, two DISJOINT fields of one struct. `pass.lec`
# refused to encode it ("WORD-LEVEL CYCLE through: and -> get_mask -> ... ->
# set_mask -> ... -> and") and took THREE defs with it -- vpu_ctrl, vpu_ml and
# vpu_tensorfma all instantiate it. See lhdsuite fixme.md issue 1b.
#
# Note a plain LOCAL struct proves either way (constprop dissolves it), so the
# struct has to be a PORT for this to bite.
#
# Cases 2 and 3 are the CONTROLS. They pass with or without the fix, and they
# are what makes case 1 meaningful: they prove the design really is acyclic, so
# case 1 failing is a lowering defect and not a genuine loop in the RTL.

set -u

LHD="${LHD:-lhd/lhd}"
if [ ! -x "$LHD" ]; then
  if [ -x ./bazel-bin/lhd/lhd ]; then
    LHD=./bazel-bin/lhd/lhd
  else
    echo "FAIL: could not find the lhd binary in $(pwd)"
    exit 1
  fi
fi

W="$(mktemp -d)"
trap 'rm -rf "$W"' EXIT

fail() { echo "FAIL: $*"; exit 1; }

# Round trip <name>: .v -> Pyrope -> lg, LEC'd against the SAME .v -> lg.
roundtrip() { # <name>
  local n="$1"
  "$LHD" compile verilog --top "$n" --emit-dir "lg:$W/${n}_ref" --workdir "$W/${n}_wr" \
    -- "$W/$n.v" >"$W/$n.ref.log" 2>&1 \
    || { tail -20 "$W/$n.ref.log"; fail "$n: Verilog -> lg failed"; }
  "$LHD" compile verilog --top "$n" --emit-dir "pyrope:$W/${n}_p" --workdir "$W/${n}_wp" \
    -- "$W/$n.v" >"$W/$n.emit.log" 2>&1 \
    || { tail -20 "$W/$n.emit.log"; fail "$n: Verilog -> Pyrope failed"; }
  "$LHD" compile "$W/${n}_p/$n.prp" --top "$n" --emit-dir "lg:$W/${n}_impl" --workdir "$W/${n}_wi" \
    >"$W/$n.impl.log" 2>&1 \
    || { tail -20 "$W/$n.impl.log"; fail "$n: recompile of emitted Pyrope failed"; }
  "$LHD" lec --impl "lg:$W/${n}_impl" --ref "lg:$W/${n}_ref" --top "$n" --workdir "$W/${n}_LW" \
    >"$W/$n.lec.log" 2>&1
}

expect_proven() { # <name> <what>
  grep -qa "PROVEN" "$W/$1.lec.log" || {
    echo "---- lec ----"; grep -aoE "WORD-LEVEL CYCLE through: [^]]{0,220}|^lec: '[^']*' [A-Z]+" "$W/$1.lec.log" | head -3
    echo "---- emitted Pyrope ----"; cat "$W/${1}_p/$1.prp"
    fail "$2"
  }
  grep -qaiE "refut|not equivalent|equiv_fail" "$W/$1.lec.log" \
    && fail "$2: REFUTED two equivalent designs"
  return 0
}

# ---------------------------------------------------------------------------
# Case 1 — NESTED struct port, disjoint fields cross-linked. THE REGRESSION.
# ---------------------------------------------------------------------------
cat >"$W/nest.v" <<'EOF'
package nest_pkg;
  typedef struct packed {
    logic        bid;
    logic        read_en;
    logic        size64;
    logic [15:0] addr;
  } req_t;
  typedef struct packed { req_t req; logic [7:0] other; } ctrl_t;
endpackage
module nest (
  input  logic enabled, input logic enabled_int, input logic req_int8,
  input  logic doing_a, input logic a_skip, input logic [15:0] addr_i,
  output nest_pkg::ctrl_t ctrl,
  output logic req_a_o
);
  logic req_a;
  always_comb begin
    ctrl.other       = 8'd0;
    ctrl.req.read_en = enabled_int && (doing_a && ~a_skip);
    ctrl.req.size64  = req_int8;
    ctrl.req.addr    = addr_i;
    // writes `bid`, reads the DISJOINT field `read_en` and a temp fed by it
    ctrl.req.bid     = ~enabled ? 1'b0 : req_int8 ? ctrl.req.read_en : req_a;
  end
  always_comb begin
    req_a = (ctrl.req.read_en || (enabled_int && a_skip)) && doing_a;
  end
  assign req_a_o = req_a;
endmodule
EOF
roundtrip nest
expect_proven nest "case 1 (NESTED struct port): a disjoint-field cross-link must not become a combinational cycle"
echo "ok: case 1 -- nested struct port round-trips equivalent"

# The INVARIANT, stated structurally: a nested field access must resolve to a
# leaf net, exactly as a one-level access does -- not to a mask of a packed
# word. Without this, case 1 could be satisfied by a smarter cycle breaker
# while the lowering stayed word-shaped.
if grep -qE '(set_mask|get_mask|#\[)' "$W/nest_p/nest.prp"; then
  echo "---- emitted Pyrope ----"; cat "$W/nest_p/nest.prp"
  fail "case 1: nested field access still lowers to packed-word masking -- leaf, bundle and variable access must behave identically"
fi
echo "ok: case 1 -- nested fields lower to leaf nets, no packed-word masking"

# ---------------------------------------------------------------------------
# Case 2 — CONTROL: the same design with a FLAT struct. Passes before + after.
# ---------------------------------------------------------------------------
cat >"$W/flat.v" <<'EOF'
package flat_pkg;
  typedef struct packed {
    logic        bid;
    logic        read_en;
    logic        size64;
    logic [15:0] addr;
    logic [7:0]  other;
  } ctrl_t;
endpackage
module flat (
  input  logic enabled, input logic enabled_int, input logic req_int8,
  input  logic doing_a, input logic a_skip, input logic [15:0] addr_i,
  output flat_pkg::ctrl_t ctrl,
  output logic req_a_o
);
  logic req_a;
  always_comb begin
    ctrl.other   = 8'd0;
    ctrl.read_en = enabled_int && (doing_a && ~a_skip);
    ctrl.size64  = req_int8;
    ctrl.addr    = addr_i;
    ctrl.bid     = ~enabled ? 1'b0 : req_int8 ? ctrl.read_en : req_a;
  end
  always_comb begin
    req_a = (ctrl.read_en || (enabled_int && a_skip)) && doing_a;
  end
  assign req_a_o = req_a;
endmodule
EOF
roundtrip flat
expect_proven flat "case 2 (FLAT struct port control)"
echo "ok: case 2 -- flat struct port (control) proves"

# ---------------------------------------------------------------------------
# Case 3 — CONTROL: nested, but the field is read through a local temp.
# Proves the RTL is genuinely acyclic; only the read-back through the struct
# was ever the problem.
# ---------------------------------------------------------------------------
sed -e 's/^module nest /module nesttmp /' \
    -e 's/  logic req_a;/  logic req_a;\n  logic read_en_l;/' \
    -e 's/    ctrl.req.read_en = enabled_int \&\& (doing_a \&\& ~a_skip);/    read_en_l = enabled_int \&\& (doing_a \&\& ~a_skip);\n    ctrl.req.read_en = read_en_l;/' \
    -e 's/req_int8 ? ctrl.req.read_en : req_a;/req_int8 ? read_en_l : req_a;/' \
    -e 's/    req_a = (ctrl.req.read_en || (enabled_int \&\& a_skip)) \&\& doing_a;/    req_a = (read_en_l || (enabled_int \&\& a_skip)) \&\& doing_a;/' \
    "$W/nest.v" >"$W/nesttmp.v"
grep -q "read_en_l" "$W/nesttmp.v" || fail "case 3: fixture rewrite produced no read_en_l"
roundtrip nesttmp
expect_proven nesttmp "case 3 (nested, field read via a local temp -- control)"
echo "ok: case 3 -- nested read via a local temp proves (the RTL is acyclic)"

# ---------------------------------------------------------------------------
# Case 4 — CONTROL: WHOLE SUB-STRUCT read feeding a SIBLING LEAF's write.
# ---------------------------------------------------------------------------
# An interior level (`ctrl.b`) is a name PREFIX of the leaves, never a leaf
# entry, so a whole-sub-struct read falls through to a reconstruct over ALL the
# port's leaves plus a slice -- an OVER-WIDE read that textually reads the very
# leaf it feeds (`ctrl.a.x`).
#
# It proves anyway, and that is the point of keeping this as a CONTROL rather
# than a gate: once the leaves are separate NETS the slice provably drops the
# sibling lanes and constprop folds the dependency away. The word-level cycle
# needed a `set_mask` CHAIN ON ONE NET, which a per-leaf port can no longer
# form. A narrower subtree-only reconstruct was implemented and then removed
# because nothing could distinguish it -- see the note in slang_expr.cpp.
#
# The shape is chosen carefully: the read must FEED the written leaf and must
# read a sub-struct DISJOINT from it. A whole-read of the sub-struct CONTAINING
# the written leaf is a genuine cycle, and a whole-read that only sinks into an
# output cannot close one at all -- both pass vacuously and prove nothing.
cat >"$W/whole.v" <<'EOF'
package whole_pkg;
  typedef struct packed { logic x; logic [3:0] y; } a_t;
  typedef struct packed { logic p; logic [3:0] q; } b_t;
  typedef struct packed { a_t a; b_t b; } ctrl_t;
endpackage
module whole (
  input  logic enabled, input logic doing_a, input logic pi, input logic [3:0] qi,
  input  logic [3:0] yi,
  output whole_pkg::ctrl_t ctrl,
  output logic req_o
);
  logic req_a;
  always_comb begin
    ctrl.b.p = pi;
    ctrl.b.q = qi;
    ctrl.a.y = yi;
    // writes a leaf of sub-struct `a` ...
    ctrl.a.x = enabled ? req_a : 1'b0;
  end
  always_comb begin
    // ... fed by a WHOLE read of the DISJOINT sub-struct `b`. Acyclic by field;
    // a fallback that reads the whole PORT drags in ctrl.a.x and cycles.
    req_a = (ctrl.b != 5'd0) && doing_a;
  end
  assign req_o = req_a;
endmodule
EOF
roundtrip whole
expect_proven whole "case 4 (whole sub-struct read feeding a sibling-leaf write -- control)"
if grep -qE '(set_mask|get_mask)' "$W/whole_p/whole.prp"; then
  echo "---- emitted Pyrope ----"; cat "$W/whole_p/whole.prp"
  fail "case 4: a whole sub-struct read still goes through packed-word masking"
fi
echo "ok: case 4 -- whole sub-struct read beside a sibling-leaf write proves (control)"

echo "PASS: packed-struct field access lowers identically at every nesting depth"
exit 0
