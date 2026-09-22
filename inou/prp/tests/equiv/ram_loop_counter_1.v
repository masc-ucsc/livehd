// NVDLA-style module-scope integer under a runtime read enable.
module ram_loop_counter_1(input en, sel, input [3:0] d,
            output [3:0] q, output signed [31:0] seen, output reg [7:0] exit_values);
  reg [3:0] r0_dout_tmp;
  integer a, b, c, z;
  always @* begin
    if (en) begin
      for (a = 0; a < 4; a = a + 1) begin
        r0_dout_tmp[a] <= d[a];
      end
      // Unlike a, b is observed outside its loops and must retain its value.
      if (sel) begin
        for (b = 0; b < 3; b = b + 1) begin end
      end else begin
        for (b = 0; b < 5; b = b + 1) begin end
      end
    end
  end
  assign q = r0_dout_tmp;
  assign seen = b;
  always @* begin
    for (c = 2; c >= 0; c = c - 1) begin end
    for (z = 5; z < 3; z = z + 1) begin end
    exit_values = {c[3:0], z[3:0]};
  end
endmodule
