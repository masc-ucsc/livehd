// Golden for spec_memory_reset_pin: reset by rb only.
module spec_memory_reset_pin(input clk, input ra_rst, input rb, input we, input [1:0] a, input [7:0] d,
                             input [1:0] ra, output [7:0] q);
  reg [7:0] mem [0:3];
  integer k;
  // docs 08: the initializer "is the power-on contents too (the `initial` contents)".
  initial for (k = 0; k < 4; k = k + 1) mem[k] = 8'd7;
  always @(posedge clk) begin
    if (rb) begin
      for (k = 0; k < 4; k = k + 1) mem[k] <= 8'd7;
    end else if (we) begin
      mem[a] <= d;
    end
  end
  assign q = mem[ra];
endmodule
