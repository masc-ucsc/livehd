// Simple scalar-state reference, using named scalar clocks.
module bit_selected_clocks (
  input clk, gate0, gate1, gate2,
  input gate, clr_,
  input [2:0] data, enable,
  output [2:0] q
);
  wire c0 = clk & gate0 & gate;
  wire c1 = clk & gate1 & gate;
  wire c2 = clk & gate2 & gate;
  reg q__bit0, q__bit1, q__bit2;
  always @(posedge c0 or negedge clr_)
    if (!clr_) q__bit0 <= 0;
    else if (enable[0]) q__bit0 <= data[0];
  always @(negedge c1 or negedge clr_)
    if (!clr_) q__bit1 <= 1;
    else if (enable[1]) q__bit1 <= data[1];
  always @(posedge c2 or negedge clr_)
    if (!clr_) q__bit2 <= 0;
    else if (enable[2]) q__bit2 <= data[2];
  assign q = {q__bit2, q__bit1, q__bit0};
endmodule
