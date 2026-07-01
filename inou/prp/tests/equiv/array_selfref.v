// Golden for a self-referencing PACKED-ARRAY LOCAL, Type C of the "false comb
// cycle" family (see small_todo_working.md) -- distinct from Type A/B above:
// this is a plain array VARIABLE, not part of any struct/io bundle at all.
// One element of the array's `'{...}` pattern assignment reads ANOTHER
// element of the SAME array being assigned in this statement, matching
// XiangShan shapes like CloseShiftLeftWithMux's `io_result_res_vec` /
// BusyTable's `io_read`.
//
// Substituting vec[0]=a_0 into vec[1]'s expression makes this trivially
// ACYCLIC (z = a_0 ^ a_1), but no existing LiveHD mechanism splits a plain
// packed-array `'{...}` pattern into independent per-element leaves the way
// is_scalar_struct_var does for packed structs (Type A/B above) -- so a read
// of `vec[0]` while computing `vec[1]` in the same pattern wires `vec[0]`
// onto the whole not-yet-fully-defined `vec` node at the graph level.
//
// On the FULL XiangShan modules (CloseShiftLeftWithMux etc.) this surfaces as
// an `lhd lec` ENCODE failure ("operand of 'X' has no encodable driver
// (combinational cycle?)"). At this minimal scale it instead surfaces as a
// silent MISCOMPILE: `lhd lec` cleanly REFUTES with a concrete counterexample
// (z wrong), and yosys lgcheck (prp-equiv, an independent tool) agrees the
// two circuits differ. Same root cause, two different downstream symptoms
// depending on design size/shape (see io_bundle_nested.v for the same
// pattern on the struct side).
//
// KNOWN BROKEN (fixme) -- see small_todo_working.md "Type C: self-referencing
// packed-array locals" for the fix plan (a new array-element leaf-splitting
// mechanism, analogous to the struct one, is needed -- there is no existing
// scaffolding for this case).
module \array_selfref.top (
  input  [1:0] a_0,
  input  [1:0] a_1,
  output [1:0] z
);
  wire [1:0][1:0] vec;
  assign vec = '{vec[0] ^ a_1, a_0};  // vec[1] reads sibling vec[0] in the same pattern
  assign z = vec[1];
endmodule
