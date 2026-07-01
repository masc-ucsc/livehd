// Golden for sim_dontcare_state: the register takes X on invalid cycles and
// the output payload is also X while invalid. This is LEC-equivalent to the
// Pyrope source, but exact SIM state/output dumps differ.
module \sim_dontcare_state.hold (
  input        clock,
  input        reset,
  input        valid,
  input  [7:0] d,
  output       valid_o,
  output [7:0] q
);
  reg [7:0] r;
  assign valid_o = valid;
  assign q = valid ? r : 8'hxx;

  always @(posedge clock) begin
    if (reset)
      r <= 8'd0;
    else if (valid)
      r <= d;
    else
      r <= 8'hxx;
  end
endmodule
