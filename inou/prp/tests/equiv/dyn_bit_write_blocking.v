// Golden for dyn_bit_write_blocking: the same design as dyn_bit_write_var,
// written the way Verilog usually says it -- BLOCKING runtime-position writes
// in a combinational process. `q` is `d` with bit `b` replaced by `x`; `r` is
// `d` with the nibble at `c` replaced by `v`, then bit `b` by `x` (the later
// write wins where they overlap); `s` does the same on a SIGNED vector.
module dyn_bit_write_blocking (
  input         [15:0] d,
  input         [3:0]  b,
  input         [2:0]  c,
  input                x,
  input         [3:0]  v,
  output        [15:0] q,
  output        [15:0] r,
  output signed [15:0] s
);
  reg        [15:0] w;
  reg        [15:0] u;
  reg signed [15:0] t;
  always @(*) begin
    w = d;
    w[b] = x;
    u = d;
    u[c +: 4] = v;
    u[b] = x;
    t = d;
    t[c +: 4] = v;
    t[b] = x;
  end
  assign q = w;
  assign r = u;
  assign s = t;
endmodule
