module \mod_acc_state.acc (
  input        clock,
  input        reset,
  input  [7:0] a,
  input        go,
  output [7:0] out
);

  reg [7:0] acc_r;
  assign out = acc_r;

  always @(posedge clock) begin
    if (reset) acc_r <= 8'd0;
    else if (go) acc_r <= acc_r ^ a;
  end

endmodule
