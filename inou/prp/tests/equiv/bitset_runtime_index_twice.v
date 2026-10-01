// Golden for bitset_runtime_index_twice.prp: plain Verilog blocking bit writes.
module bitset_runtime_index_twice (
  input  [2:0] i,
  input  [2:0] j,
  input        e,
  input        f,
  input  [7:0] a,
  input        k,
  input        g,
  input        h,
  input  signed [3:0] x4,
  output [7:0] z,
  output [7:0] y,
  output [1:0] n,
  output [7:0] c,
  output signed [7:0] sz,
  output       sn,
  output signed [7:0] sx,
  output signed [7:0] wz,
  output       wn
);
  reg [7:0] q;
  reg [7:0] p;
  reg [7:0] r;
  reg signed [7:0] sq;
  reg signed [7:0] so;
  reg signed [7:0] sw;
  always @* begin
    q    = 8'd0;
    q[i] = e;
    q[j] = f;
    p    = 8'h5A;
    p[i] = e;
    p[j] = f;
    r    = a;
    r[i] = e;
    r[j] = f;
    sq    = -8'sd1;
    sq[i] = e;
    sq[j] = f;
    so    = -8'sd3;
    so[i] = e;
    sw    = x4;
    sw[i] = e;
  end
  assign z = q;
  assign y = p;
  assign n = k ? {g, h} : {h, g};
  assign c = r;
  assign sz = sq;
  assign sn = sq < 0;
  assign sx = so;
  assign wz = sw;
  assign wn = sw < 0;
endmodule
