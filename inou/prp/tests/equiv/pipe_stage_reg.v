module \pipe_stage_reg.split (
  input            clock,
  input            reset,
  input      [7:0] a,
  input      [7:0] b,
  output     [8:0] x
);

  reg [8:0] tmp;

  always @(posedge clock) begin
    if (reset) tmp <= 9'd0;
    else       tmp <= a + b;
  end

  assign x = tmp;

endmodule
