// Golden for dyn_bit_write_var, bit by bit: `q` is `d` with bit `b` replaced
// by `x`; `r` is `d` with the nibble at `c` replaced by `v`, then bit `b` by
// `x` (the later write wins where they overlap).
module dyn_bit_write_var (
  input  [15:0] d,
  input  [3:0]  b,
  input  [2:0]  c,
  input         x,
  input  [3:0]  v,
  output [15:0] q,
  output [15:0] r
);
  wire [18:0] vv = {15'd0, v} << c;
  wire [18:0] vm = 19'hf << c;
  reg  [15:0] w;
  reg  [15:0] u;
  integer     i;
  always @(*) begin
    for (i = 0; i < 16; i = i + 1) begin
      w[i] = (i == b) ? x : d[i];
      u[i] = (i == b) ? x : vm[i] ? vv[i] : d[i];
    end
  end
  assign q = w;
  assign r = u;
endmodule
