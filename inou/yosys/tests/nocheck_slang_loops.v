// Constant-bound procedural loops through --reader slang: for (unrolled),
// repeat (lower_while_loop), and % (LNAST mod op). nocheck_ keeps the file
// out of the plain-yosys glob (SV int declarations).
module slang_loops(input [7:0] a, output [7:0] y_for, output [7:0] y_rep, output [7:0] y_mod);
  reg [7:0] acc_f;
  always @(*) begin
    acc_f = 8'd0;
    for (int i = 0; i < 4; i = i + 1) begin
      acc_f = acc_f + a;
    end
  end
  assign y_for = acc_f;

  reg [7:0] acc_r;
  always @(*) begin
    acc_r = 8'd0;
    repeat (2) begin
      acc_r = acc_r + 8'd5;
    end
  end
  assign y_rep = acc_r + a;

  assign y_mod = a % 8'd5;
endmodule
