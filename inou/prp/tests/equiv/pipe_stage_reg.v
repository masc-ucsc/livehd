module \pipe_stage_reg.split (
  input            clock,
  input            reset,
  input      [7:0] a,
  input      [7:0] b,
  output reg [8:0] x
);

  always @(posedge clock) begin
    if (reset) x <= 9'd0;
    else       x <= a + b;
  end

endmodule
