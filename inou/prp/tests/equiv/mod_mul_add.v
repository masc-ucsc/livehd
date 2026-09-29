module \mod_mul_add.mul (
  input             clock,
  input      [15:0] a,
  input      [15:0] b,
  output reg [31:0] c
);

  always @(posedge clock) begin
    c <= a * b;
  end

endmodule

module \mod_mul_add.add (
  input             clock,
  input      [31:0] a,
  input      [31:0] b,
  output reg [32:0] c
);

  always @(posedge clock) begin
    c <= {1'b0, a} + {1'b0, b};
  end

endmodule

module \mod_mul_add.multiply_add (
  input             clock,
  input      [15:0] in1,
  input      [15:0] in2,
  output     [32:0] out
);

  wire [31:0] mul_c;
  wire [32:0] add_c;
  reg  [31:0] tmp0, tmp;
  reg  [15:0] d0, d1, in1_d;

  \mod_mul_add.mul u_mul (.clock(clock), .a(in1), .b(in2), .c(mul_c));
  \mod_mul_add.add u_add (.clock(clock), .a(tmp), .b({16'b0, in1_d}), .c(add_c));

  always @(posedge clock) begin
    tmp0  <= mul_c;
    tmp   <= tmp0;
    d0    <= in1;
    d1    <= d0;
    in1_d <= d1;
  end

  assign out = add_c;

endmodule
