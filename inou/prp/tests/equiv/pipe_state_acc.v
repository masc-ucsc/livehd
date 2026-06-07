module \pipe_state_acc.acc_mix (
  input             clock,
  input             reset,
  input      [7:0]  a,
  input      [7:0]  b,
  output reg [20:0] x
);

  reg [19:0] tmp;

  always @(posedge clock) begin
    if (reset) tmp <= 20'd0;
    else if (tmp < 20'd1000000) tmp <= tmp + a + b;
  end

  always @(posedge clock) begin
    x <= tmp + a;
  end

endmodule
