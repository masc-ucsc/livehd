module \mod_mut_delay.f (
  input            clock,
  input      [7:0] a,
  output reg [7:0] x
);

  reg [7:0] d0;

  always @(posedge clock) begin
    d0 <= a;
    x  <= d0;
  end

endmodule
