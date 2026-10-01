// Golden: `r` is clocked by `clk`, `s` by `clock`; both have a synchronous
// active-high reset on `rst` (the module's single implicit reset).
module implicit_clock_two_explicit(input clock, input clk, input rst, input [3:0] d,
                                   output [3:0] q, output [4:0] p);
  reg [3:0] r;
  reg [4:0] s;
  always @(posedge clk) begin
    if (rst) r <= 4'd5;
    else     r <= d;
  end
  always @(posedge clock) begin
    if (rst) s <= 5'd9;
    else     s <= {1'b0, d} + 5'd1;
  end
  assign q = r;
  assign p = s;
endmodule
