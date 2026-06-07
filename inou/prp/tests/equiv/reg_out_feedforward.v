module \reg_out_feedforward.addreg (
  input            clock,
  input      [7:0] a,
  input      [7:0] b,
  output reg [8:0] q
);

  always @(posedge clock) begin
    q <= a + b;
  end

endmodule
