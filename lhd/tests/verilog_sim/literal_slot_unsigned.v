// `x / (b | 1'b1)` with `b | 1` folded to a 1-bit unsigned literal boundary slot: read
// inline as a signed Slop<1> it was -1 and the quotient came out negated.
module literal_slot_unsigned(
  input clock,
  input reset,
  input [7:0] i0,
  input [15:0] i1,
  input signed [47:0] i2,
  output [1:0] o0
);
  reg [3:0] t2;
  always @(posedge clock) begin
    if (reset) t2 <= 11;
  end
  reg [47:0] t4;
  always @(posedge clock) begin
    if (reset) t4 <= 37116;
  end
  reg signed [7:0] l0;
  always @(*) if (!clock && (reset || (({2{i0[4:1]}} ^ (i2 <<< 0))))) l0 = reset ? 0 : (t4 + 6);
  wire [69:0] t1 = i1;
  assign o0 = (((0 ? l0 : t1) / (i2[6] | 1'b1)) + (l0[2] < (t2 + 1)));
endmodule
