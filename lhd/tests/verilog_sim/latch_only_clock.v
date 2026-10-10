// A design whose only state is a latch: `clock` is a clock only through the
// latch enable. It used to stay a plain data input nothing toggles, so the
// latch never opened.
module latch_only_clock(input clock, input reset, input e, input [7:0] d, output [7:0] o0);
  reg [7:0] l1;
  always @(*) if (clock && (reset || e)) l1 = reset ? 8'd0 : d;
  assign o0 = l1;
endmodule
