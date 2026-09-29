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
  reg [7:0] \bank.x ;
  reg [3:0] \bank.y ;

  always @(posedge clock) begin
    if (reset) begin
      \bank.x  <= 8'd20;
      \bank.y  <= 4'd3;
    end else if (en) begin
      \bank.x  <= wx;
      \bank.y  <= wy;
    end
  end

  assign ox = \bank.x ;
  assign oy = \bank.y ;

endmodule
