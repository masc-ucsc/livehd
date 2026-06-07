module \mod_delay3.delay3 (
  input            clock,
  input      [7:0] a,
  output reg [7:0] x
);

  reg [7:0] d0, d1;

  always @(posedge clock) begin
    d0 <= a;
    d1 <= d0;
    x  <= d1;
  end

endmodule
