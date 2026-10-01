// Golden for shl_not_wide_amount: a one-hot mask `1 << k` whose amount is
// wider than, or reaches past, the 16-bit result. Each flip is written in
// 16-bit context, so an amount at or past 16 clears nothing; `c4` is the
// always-block runtime bit write (32-bit index `i*4 + j`).
module shl_not_wide_amount (
  input             clock,
  input      [31:0] k,
  input      [7:0]  k8,
  input      [1:0]  i,
  input      [1:0]  j,
  input             x,
  input      [15:0] v,
  output     [15:0] c1,
  output     [15:0] c2,
  output     [15:0] c3,
  output reg [15:0] c4,
  output     [15:0] c5,
  output     [15:0] c6,
  output     [15:0] q
);
  wire [15:0] one = 16'd1;
  assign c1 = v & ~(one << k);
  assign c2 = v & ~(one << k);
  assign c3 = v & ~(one << k8);
  assign c5 = c1;
  assign c6 = v;  // 1 << 20 and 1 << 40 are past the 16-bit result

  always @* begin
    c4 = v;
    c4[i*4 + j] = x;
  end

  reg [15:0] r;
  always @(posedge clock) r <= v & ~(one << k);
  assign q = r;
endmodule
