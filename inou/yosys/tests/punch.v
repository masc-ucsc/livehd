
module punch(output logic [3:0] total);

  logic [2:0] out1;
  logic [2:0] out2;

  top_down1 tdown1(.out(out1)); // 2
  top_down2 tdown2(.out(out2)); // 3

  logic [2:0] x_top;

  always_comb begin
    // x_top = 0;
    tdown1.x_foo = 1;
    total = x_top + out1 + out2; // 9 == 4 + 2 + 3
  end

endmodule

module top_down1(output logic [2:0] out);

  logic [1:0] x_foo;

  always_comb begin
    top.x_top = 4;
    out = x_foo + 1; // 1 + 1
  end

endmodule

module top_down2(output logic [2:0] out);

  always_comb begin
    out = top.tdown1.x_foo + 2; // out = 3
  end

endmodule

