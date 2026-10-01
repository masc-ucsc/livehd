// Golden for spec_memory_multidim_rowmajor: one flat 32-entry memory, row-major.
module spec_memory_multidim_rowmajor(input clk, input we, input [1:0] wi, input [2:0] wj, input [7:0] d,
                                     input [1:0] ri, input [2:0] rj, output [7:0] q);
  reg [7:0] b [0:31];
  always @(posedge clk) begin
    if (we) b[{wi, wj}] <= d;
  end
  assign q = b[{ri, rj}];
endmodule
