// Golden for untyped_dyn_bit_writes: the same writes on sized variables.
module untyped_dyn_bit_writes (
  input         [2:0]  i,
  input         [2:0]  j,
  input                e,
  input                f,
  input         [2:0]  c,
  input         [2:0]  b,
  input         [3:0]  v,
  input         [1:0]  w,
  output        [7:0]  z,
  output        [15:0] r,
  output signed [15:0] sn
);
  reg        [7:0]  q;
  reg        [15:0] p;
  reg signed [15:0] s;
  always @(*) begin
    q = 8'd0;
    q[i] = e;
    q[j] = f;
    p = 16'd0;
    p[c +: 4] = v;
    p[b +: 2] = w;
    s = -16'sd1;
    s[i] = e;
    s[j] = f;
  end
  assign z  = q;
  assign r  = p;
  assign sn = s;
endmodule
