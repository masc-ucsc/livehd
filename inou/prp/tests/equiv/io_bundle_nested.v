// Golden for CIRCT/firtool's synthetic-`io`-bundle idiom, Type B: same shape
// as io_bundle_selfref.v (a false self-reference from reading a sibling field
// within the SAME `'{...}` pattern assignment), but here one field of the
// bundle is ITSELF a struct (`sub`), matching XiangShan shapes like
// CSRPermitModule's `io.toS1.vd` / `io.xRet.mret` and ByteMaskTailGen /
// DstMgu / CVT32ModuleS1 / DecodeUnit / EnqEntry_26.
//
// On the FULL XiangShan modules (CSRPermitModule etc.) the pre-fix bug surfaced
// as an `lhd lec` ENCODE failure ("operand of 'X' has no encodable driver
// (combinational cycle?)" -- a literal graph cycle in the bigger design). At
// this minimal scale it instead surfaced as a silent MISCOMPILE: `lhd lec`
// cleanly REFUTED with a concrete counterexample (io_c wrong), and yosys
// lgcheck (prp-equiv, an independent tool) agreed the two circuits differ --
// the flat-bus fallback wired `c` to the wrong value rather than forming a
// literal cycle here. Same root cause, two different downstream symptoms
// depending on design size/shape.
//
// FIXED (2026-06-30) in slang_structure.cpp: a plain (non-array) NESTED STRUCT
// field no longer forces the whole `io` onto a flat bus when the struct is only
// deep-READ (not whole-copied / deep-written). `is_scalar_struct_var` now uses
// field_forces_flat_bus() (nested struct is bundle-safe; only an array-of-struct
// field forces flat), and the Struct_whole_copy_collector no longer treats a
// struct's own `'{...}` pattern-assign LHS as a whole-copy. `io` splits into
// per-field leaf nets (`io.sub` its own net), so `c`'s read of `io.sub.x` routes
// to the independent leaf and the false self-reference is gone. This test guards
// the fix (both prp-equiv-io_bundle_nested and prp-v2prp-io_bundle_nested PASS).
module \io_bundle_nested.top (
  input        io_x,
  input  [1:0] io_a,
  output [1:0] io_c
);
  wire struct packed { logic [1:0] a; struct packed { logic x; } sub; logic [1:0] c; } io;
  assign io = '{a: io_a, sub: '{x: io_x}, c: (io.sub.x ? io_a : 2'b0)};
  assign io_c = io.c;
endmodule
