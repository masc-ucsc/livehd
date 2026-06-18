// Golden for a packed 2-D array dynamic element select (XiangShan
// MiscResultSelect / ShiftResultSelect shape: `wire [N:0][W:0] g = {...};
// out = g[idx]`).  g is packed MSB-first from {d,c,b,a}, so g[0]=a .. g[3]=d;
// `z = g[sel]` is a plain runtime element select.
//
// --reader slang MISCOMPILES this: its netlist is NOT logically equivalent to
// this golden (verified vs the original source with yosys equiv), while BOTH
// --reader yosys-verilog and --reader yosys-slang lower it correctly.  Minimal
// repro distilled from the XiangShan XSCore reader sweep.
module \arr2d_dyn_sel.sel2d (
  input  [1:0] sel,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output [7:0] z
);
  wire [3:0][7:0] g = {d, c, b, a};   // g[0]=a, g[1]=b, g[2]=c, g[3]=d
  assign z = g[sel];
endmodule
