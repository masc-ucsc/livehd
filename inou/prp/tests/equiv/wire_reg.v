module \wire_reg.wreg (input clock, input reset, input [7:0] a, output [8:0] o);
  wire [8:0] nx = a + 1;     // nx is the next-state value, available this cycle
  reg  [8:0] r;              // the .prp's `r`: loaded from nx, never read (state-name pairing)
  always @(posedge clock) begin
    if (reset) r <= 9'd0;
    else       r <= nx;
  end
  assign o = nx;
endmodule
