/*
:lec_expect: refuted
*/
module dut(input g, input [7:0] d, output reg [7:0] q);
  always_latch begin
    if (!g)
      q <= d;
  end
endmodule
