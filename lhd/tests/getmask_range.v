module getmask_range(
  input  logic [63:0] a,
  input  logic [5:0]  amount,
  output logic [7:0]  field,
  output logic [63:0] shifted
);
  // Deliberately spell the slice as SRA + low mask. pass/cprop must collapse
  // this to one ranged Get_mask selecting a[19:12].
  assign field   = (a >> 12) & 64'hff;
  assign shifted = a >> amount;
endmodule
