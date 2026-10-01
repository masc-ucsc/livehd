// Golden for reg_output_const_reset.prp: q resets to RST = 8, p to RST2 = 5.
module reg_output_const_reset (
  input            clock,
  input            reset,
  input            en,
  input      [3:0] d,
  output reg [3:0] q,
  output reg [3:0] p
);
  always @(posedge clock) begin
    if (reset) begin
      q <= 4'd8;
      p <= 4'd5;
    end else if (en) begin
      q <= d;
      p <= d ^ 4'd3;
    end
  end
endmodule
