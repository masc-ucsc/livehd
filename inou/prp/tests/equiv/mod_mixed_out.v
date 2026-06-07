module \mod_mixed_out.mix (
  input            clock,
  input      [7:0] a,
  output     [7:0] x,
  output reg [7:0] y
);

  reg [7:0] d0;

  assign x = a+1;

  always @(posedge clock) begin
    d0 <= a;
    y  <= d0;
  end

endmodule
