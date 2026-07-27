// todo/livehd/2f-latch + lhdsuite fixme issue 1h — MINIMAL reproducer for a
// SILENT MISCOMPILE in the Verilog -> Pyrope -> LGraph round trip.
//
// `assign arr[0] = 1 ? d : ~d;` emits Pyrope of the shape
//     mut _mux_1:u1 = 0        // declared here...
//     ...
//     _mux_1 = d               // ...assigned LATER
//     arr[0] = _mux_1          // <-- stores the DECLARATION value (0), not d
// so `o` reads 0 instead of `d` and the round trip is not equivalent.
//
// Verified minimal: both ingredients are required.
//   * the read index must be NON-CONSTANT (`arr[sel]`). With `arr[0]` the front
//     end degenerates the array to a scalar bit-select and the bug is bypassed.
//   * the RHS must be a `mut` scalar DECLARED in one statement and ASSIGNED in
//     another. A ternary on an INPUT emits `mut _mux_1 = if c {..} else {..}`
//     (decl+init in ONE statement) and is CORRECT; only an elaboration-constant
//     condition with non-const-folded arms produces the split shape.
// NOT required: package, parameter, generate block, packed struct, enum,
// width > 1, or more than one array element.
module z2(input logic sel, input logic d, output logic o);
  logic arr [2];
  assign arr[0] = 1 ? d : ~d;
  assign o = arr[sel];
endmodule
