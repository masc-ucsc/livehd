// Golden for untyped_range_write_wide_index: two part-select writes at a
// 13-bit position into a zeroed vector; only its low 16 bits are observed.
// Verilog drops the bits of a window past the top, so a 19-bit vector holds
// every window that can reach bits 0..15.
module untyped_range_write_wide_index (
  input         sel,
  input  [12:0] big,
  input  [2:0]  c,
  input  [2:0]  b,
  input  [3:0]  v,
  input  [1:0]  w,
  output [15:0] r
);
  wire [12:0] cc = sel ? big : {10'd0, c};
  wire [12:0] bb = sel ? big : {10'd0, b};
  reg  [18:0] p;
  always @(*) begin
    p = 19'd0;
    p[cc +: 4] = v;
    p[bb +: 2] = w;
  end
  assign r = p[15:0];
endmodule
