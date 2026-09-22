// Straight-line spelling of the RAM data latch and retained terminal value.
module ram_loop_counter(input en, sel, input [3:0] d,
            output [3:0] q, output signed [31:0] seen, output [7:0] exit_values);
  reg [3:0] r0_dout_tmp;
  integer b;
  always @* begin
    if (en) begin
      r0_dout_tmp <= d;
      if (sel) b = 3; else b = 5;
    end
  end
  assign q = r0_dout_tmp;
  assign seen = b;
  assign exit_values = 8'hf5;
endmodule
