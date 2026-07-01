// Golden for a self-referencing PACKED-ARRAY LOCAL, Type C of the "false comb
// cycle" family (see small_todo_working.md) -- distinct from Type A/B above:
// this is a plain array VARIABLE, not part of any struct/io bundle at all.
// One element of the array's `'{...}` pattern assignment reads ANOTHER
// element of the SAME array being assigned in this statement, matching
// XiangShan shapes like CloseShiftLeftWithMux's `io_result_res_vec` /
// BusyTable's `io_read`.
//
// Substituting vec[0]=a_0 into vec[1]'s expression makes this trivially
// ACYCLIC (z = a_0 ^ a_1). Pre-fix, no LiveHD mechanism split a plain
// packed-array `'{...}` pattern into independent per-element leaves the way
// is_scalar_struct_var does for packed structs (Type A/B above) -- so a read
// of `vec[0]` while computing `vec[1]` in the same pattern wired the stale
// whole-`vec` bus (poison) onto vec[1], miscompiling z.
//
// On the FULL XiangShan modules (CloseShiftLeftWithMux etc.) the pre-fix bug
// surfaced as an `lhd lec` ENCODE failure ("operand of 'X' has no encodable
// driver (combinational cycle?)"). At this minimal scale it instead surfaced as
// a silent MISCOMPILE: `lhd lec` cleanly REFUTED with a concrete counterexample
// (z wrong), and yosys lgcheck (prp-equiv, an independent tool) agreed the two
// circuits differ. Same root cause, two symptoms by design size/shape (see
// io_bundle_nested.v for the same pattern on the struct side).
//
// FIXED (2026-06-30) in slang_structure.cpp: Array_selfref_collector flags a
// packed-array local whose `'{...}` pattern reads one of its own elements, and
// declare_array_leaves splits it into per-element WIRE leaf nets (`vec.e0`,
// `vec.e1`) -- the array analogue of the per-field struct bundle. A sibling read
// `vec[0]` routes to its own leaf net (via the ElementSelect intercept in
// slang_expr.cpp / slang_lvalue.cpp), breaking the false cycle. This test guards
// the fix (both prp-equiv-array_selfref and prp-v2prp-array_selfref PASS).
module \array_selfref.top (
  input  [1:0] a_0,
  input  [1:0] a_1,
  output [1:0] z
);
  wire [1:0][1:0] vec;
  assign vec = '{vec[0] ^ a_1, a_0};  // vec[1] reads sibling vec[0] in the same pattern
  assign z = vec[1];
endmodule
