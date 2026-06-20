module \reg_tuple_reset.rtr (
  input            en,
  input      [7:0] wx,
  input      [3:0] wy,
  output     [7:0] ox,
  output     [3:0] oy,
  input            clock,
  input            reset
);

  // The tuple register is split per field: one Flop for `.x`, one for `.y`,
  // each with its own slice of the reset value (x<=20, y<=3).
  reg [7:0] bx;
  reg [3:0] by;

  always @(posedge clock) begin
    if (reset) begin
      bx <= 8'd20;
      by <= 4'd3;
    end else if (en) begin
      bx <= wx;
      by <= wy;
    end
  end

  assign ox = bx;
  assign oy = by;

endmodule
