// Golden for CIRCT/firtool's synthetic-`io`-bundle idiom: a module's ports are
// packed into one local `struct packed {...} io;` purely for readability, and
// one field's value is computed by reading a SIBLING field of the SAME
// `'{...}` pattern assignment (`c: ... io.b ...`). This is the minimal
// distillation of the XiangShan AgeDetector shape
// (../xs/repo/build/rtl/AgeDetector.sv):
//   assign io = '{enq: io_enq, canIssue: io_canIssue, out: <expr using io.canIssue>};
//
// All three fields here are PLAIN packed arrays (no nested struct) -- Type A
// of the "io-bundle false comb cycle" family (see small_todo_working.md).
// `--reader slang` used to flatten `io` into one monolithic wire whenever ANY
// field was not a simple bit vector (is_scalar_struct_var's isSimpleBitVector()
// gate rejected multi-dim-array fields too), so the `c` field's read of `io.b`
// became a literal self-reference on the SAME flat bus `io` is being assigned
// in this very statement -- a false combinational cycle (`lhd lec` reported
// "operand of 'X' has no encodable driver (combinational cycle?)") even though
// `c` and `b` are independent leaves.
//
// FIXED (2026-06-30) by field_type_is_struct_free() in slang_structure.cpp: a
// plain (possibly multi-dim) array field no longer forces the flat-bus
// fallback, so `io` splits into independent per-field leaf nets and the false
// self-reference is gone. This test guards the fix (prp-v2prp-io_bundle_selfref
// must PASS, not fixme).
module \io_bundle_selfref.top (
  input  [1:0] io_a,
  input  [1:0] io_b,
  output [1:0] io_c
);
  wire struct packed { logic [1:0] a; logic [1:0] b; logic [1:0] c; } io;
  assign io = '{a: io_a, b: io_b, c: io.b & io_a};
  assign io_c = io.c;
endmodule
