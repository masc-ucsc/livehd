// add2
module add2 (
  input i1,
  input i2,
  input c_in,
  output o,
  output c_out
);

  reg sum;
  reg carry_out;
  wire sum_neg;

  assign sum_neg = ~sum;
  assign o = sum_neg;
  assign c_out = carry_out;
  
  always @(*) begin
    if (c_in == 1'b0) begin
      sum = i1^i2;
      carry_out = i1&i2;
    end else begin
      sum = ~(i1^i2);
      carry_out = i1|i2;
    end
  end
endmodule

// add3
module add3 (
  input i1,
  input i2,
  input i3,
  input c_in,
  output [1:0] o
);

  wire inter_sum_2;
  wire c_out_2;
  wire [1:0] sum3;

  add2 add2_0(
    .i1(i1),
    .i2(i2),
    .c_in(c_in),
    .o(inter_sum_2),
    .c_out(c_out_2)
  );

  add2 add2_1(
    .i1(~inter_sum_2),
    .i2(i3),
    .c_in(c_out_2),
    .o(sum3[0]),
    .c_out(sum3[1])
  );

  assign o = sum3;
endmodule


