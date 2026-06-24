module \pipe_state_acc.acc_mix (
  input             clock,
  input             reset,
  input      [3:0]  a,
  input      [3:0]  b,
  output reg [8:0]  x
);

  reg [7:0] tmp;

  always @(posedge clock) begin
    if (reset) tmp <= 8'd0;
    else if (tmp < 8'd200) tmp <= tmp + a + b;
  end

  always @(posedge clock) begin
    x <= tmp + a;
  end

endmodule
