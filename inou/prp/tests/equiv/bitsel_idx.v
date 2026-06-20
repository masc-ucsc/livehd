// Golden for a runtime single-bit select `z = g[idx]` (the NATIVE Pyrope form
// `g#[idx]`).  Verifies the dynamic bit-index lowering against a plain Verilog
// variable-index element select.
module \bitsel_idx.bsel (
  input  [15:0] g,
  input  [3:0]  idx,
  output        z
);
  assign z = g[idx];
endmodule
