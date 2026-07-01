// Golden for async_reset_enable: data flop with an explicit async reset input
// independent of the module's implicit reset.
module \async_reset_enable.pipe (
  input        clock,
  input        reset,
  input        en,
  input        arst,
  input  [7:0] d,
  output [7:0] q
);
  reg [7:0] r;
  assign q = r;

  always @(posedge clock or posedge reset or posedge arst) begin
    if (reset || arst)
      r <= 8'd0;
    else if (en)
      r <= d;
  end
endmodule
