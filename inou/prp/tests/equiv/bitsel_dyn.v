// Golden for a runtime (variable-index) single-bit select `z = g[idx]` on an
// unsigned vector.  The most minimal form of the XiangShan FTBEntryGen shape
// (`_GEN[io_cfiIndex_bits]` etc.).
//
// --reader slang MISCOMPILES this: it lowers the dynamic bit-select to an
// arithmetic-shift expression over a value it treats as signed
// (`(g<<1) >>> (idx+1) & 1`), so for inputs whose high bit is set the
// sign-extended shift returns the wrong bit (counterexample g=16'hF341 ->
// slang yields 0 where the correct select is 1).  --reader yosys-verilog and
// --reader yosys-slang are both correct.  Same root cause as arr2d_dyn_sel
// (dynamic select lowering), shown here at 1-bit granularity.
module \bitsel_dyn.bsel (
  input  [15:0] g,
  input  [3:0]  idx,
  output        z
);
  assign z = g[idx];
endmodule
