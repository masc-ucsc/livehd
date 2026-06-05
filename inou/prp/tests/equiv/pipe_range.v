module \pipe_range.flex (
  input            clock,
  input      [7:0] a,
  output reg [8:0] d
);

  reg [8:0] s0;

  always @(posedge clock) begin
    s0 <= a + 9'd1;
    d  <= s0;
  end

endmodule
