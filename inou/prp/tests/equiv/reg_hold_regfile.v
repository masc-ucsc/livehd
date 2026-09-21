module regfile(input we_a, input we_b, input [7:0] adata, input [7:0] bdata,
               output [7:0] q, input clock, input reset);
  reg [7:0] r;
  always @(posedge clock)
    if (reset)      r <= 8'b0;
    else if (we_b)  r <= bdata;
    else if (we_a)  r <= adata;
  assign q = r;
endmodule
