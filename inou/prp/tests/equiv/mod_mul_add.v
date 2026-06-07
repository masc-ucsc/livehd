module \mod_mul_add.multiply_add (
  input             clock,
  input      [15:0] in1,
  input      [15:0] in2,
  output reg [32:0] out
);

  reg [31:0] m0, m1, m2;       // mul: 3 stages (bare pipe picked at 3)
  reg [15:0] d0, d1, d2;       // in1 aligned by 3
  // adder: 1 stage; out lands at cycle 4

  always @(posedge clock) begin
    m0  <= in1 * in2;
    m1  <= m0;
    m2  <= m1;
    d0  <= in1;
    d1  <= d0;
    d2  <= d1;
    out <= {1'b0, m2} + {17'b0, d2};
  end

endmodule
