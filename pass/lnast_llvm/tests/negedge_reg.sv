// A posedge and a negedge register feeding each other: the falling edge sees the value the rising
// edge just loaded, and the next rising edge sees what the falling edge loaded.
module negedge_reg(input clk, input rst, input [7:0] d, output [7:0] q, output [7:0] qn);
  logic [7:0] p, n;
  always_ff @(posedge clk) if (rst) p <= 8'd0; else p <= d + n;
  always_ff @(negedge clk) n <= p ^ d;
  assign q  = p;
  assign qn = n;
endmodule
