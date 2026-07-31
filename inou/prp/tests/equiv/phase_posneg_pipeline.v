// Same machine as phase_posneg_pipeline.prp, written as two explicit edge
// processes. The negedge process must observe the posedge commit of `rise`.
module phase_posneg_pipeline (
  input  wire       clk,
  input  wire [3:0] d,
  output wire [3:0] rise_o,
  output wire [3:0] fall_o
);
  reg [3:0] rise;
  reg [3:0] fall;

  always @(posedge clk)
    rise <= fall + d;

  always @(negedge clk)
    fall <= rise ^ d;

  assign rise_o = rise;
  assign fall_o = fall;
endmodule
