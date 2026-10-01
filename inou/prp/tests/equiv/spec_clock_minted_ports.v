// Golden for spec_clock_minted_ports.prp: minted `clock` (posedge) and `reset`
// (active-high, synchronous by default: docs 04b `compile.upass.reset_style`
// default `sync`).
module spec_clock_minted_ports(input clock, input reset, input en, output [7:0] q);
  reg [7:0] c;
  always @(posedge clock) begin
    if (reset) c <= 8'd3;
    else if (en) c <= c + 8'd1;
  end
  assign q = c;
endmodule
