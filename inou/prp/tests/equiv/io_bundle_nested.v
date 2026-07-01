// Golden for CIRCT/firtool's synthetic-`io`-bundle idiom, Type B: same shape
// as io_bundle_selfref.v (a false self-reference from reading a sibling field
// within the SAME `'{...}` pattern assignment), but here one field of the
// bundle is ITSELF a struct (`sub`), matching XiangShan shapes like
// CSRPermitModule's `io.toS1.vd` / `io.xRet.mret` and ByteMaskTailGen /
// DstMgu / CVT32ModuleS1 / DecodeUnit / EnqEntry_26.
//
// is_scalar_struct_var's field_type_is_struct_free() gate (slang_structure.cpp,
// landed 2026-06-30 for Type A / plain-array fields) rejects ANY struct field
// -- nested or not -- and falls back to ONE flat bus for the whole `io`, so
// `c`'s read of `io.sub.x` becomes a false self-reference again.
//
// On the FULL XiangShan modules (CSRPermitModule etc.) this surfaces as an
// `lhd lec` ENCODE failure ("operand of 'X' has no encodable driver
// (combinational cycle?)" -- a literal graph cycle in the bigger design). At
// this minimal scale it instead surfaces as a silent MISCOMPILE: `lhd lec`
// cleanly REFUTES with a concrete counterexample (io_c wrong), and yosys
// lgcheck (prp-equiv, an independent tool) agrees the two circuits differ --
// so the flat-bus fallback wires `c` to the wrong value rather than forming a
// literal cycle here. Same root cause, two different downstream symptoms
// depending on design size/shape.
//
// KNOWN BROKEN (fixme) -- see small_todo_working.md "Type B: nested-struct io
// bundles" for the fix plan (assign_struct_whole needs to recurse into nested
// struct fields -- give `sub` its own per-field leaves too -- instead of
// falling back to a flat bus the moment ANY field is a struct).
module \io_bundle_nested.top (
  input        io_x,
  input  [1:0] io_a,
  output [1:0] io_c
);
  wire struct packed { logic [1:0] a; struct packed { logic x; } sub; logic [1:0] c; } io;
  assign io = '{a: io_a, sub: '{x: io_x}, c: (io.sub.x ? io_a : 2'b0)};
  assign io_c = io.c;
endmodule
