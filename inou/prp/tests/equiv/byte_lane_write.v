// Golden for byte_lane_write: the byte-lane write idiom, registered and
// combinational. The index expressions are 32 bits wide (`k*8`) or 5
// (`{k, 3'b000}`), but their VALUES stay in 0..24, so the 8-bit window never
// leaves the 32-bit vector.
module byte_lane_write (
  input             clock,
  input             reset,
  input      [1:0]  k,
  input             we,
  input      [7:0]  byte_i,
  input      [31:0] v,
  output     [31:0] q,
  output reg [31:0] c
);
  reg [31:0] w;
  always @(posedge clock) begin
    if (reset) w <= 32'd0;
    else if (we) w[k*8 +: 8] <= byte_i;
  end
  assign q = w;
  always @(*) begin
    c = v;
    c[{k, 3'b000} +: 8] = byte_i;
  end
endmodule
