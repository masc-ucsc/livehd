// Golden for mem_partial_write_lanes: Verilog nonblocking part-select writes
// merge per bit (a later write wins where they overlap), and each same-cycle
// read is spelled out for its ordering.
module mem_partial_write_lanes(input clock, input we0, input we1, input we2, input we3, input [1:0] a, input [1:0] b, input [1:0] c,
           input [7:0] x, input [7:0] y, input [7:0] z, input [15:0] w, input [3:0] k, input [2:0] j, input bv, input [3:0] nib,
           input [1:0] ra,
           output [15:0] qp0, output [15:0] qp1, output [15:0] qo, output [15:0] qf, output [15:0] qn, output [15:0] qr);
  reg [15:0] P[0:3];
  reg [15:0] O[0:3];
  reg [15:0] F[0:3];
  reg [15:0] N[0:3];
  reg [15:0] R[0:3];
  always @(posedge clock) begin
    if (we0) begin P[a][7:0] <= x; O[a][7:0] <= x; F[a][7:0] <= x; N[a][7:0] <= x; end
    if (we1) begin P[b][15:8] <= y; O[b][15:8] <= y; F[b][15:8] <= y; N[b][15:8] <= y; end
    if (we2) begin P[c][11:4] <= z; O[c][11:4] <= z; F[c][11:4] <= z; N[c][11:4] <= z; end
    if (we3) R[c] <= w;
    if (we0) R[a][k] <= bv;
    if (we1) R[b][j +: 4] <= nib;
  end
  // The entry a same-cycle read after every write sees (per-bit, later write wins).
  reg [15:0] p_after, f_after, r_after;
  always @* begin
    p_after = P[ra];
    if (we0 && a == ra) p_after[7:0]  = x;
    if (we1 && b == ra) p_after[15:8] = y;
    if (we2 && c == ra) p_after[11:4] = z;
    f_after = F[ra];
    if (we0 && a == ra) f_after[7:0]  = x;
    if (we1 && b == ra) f_after[15:8] = y;
    if (we2 && c == ra) f_after[11:4] = z;
    r_after = R[ra];
    if (we3 && c == ra) r_after = w;
    if (we0 && a == ra) r_after[k] = bv;
    if (we1 && b == ra) r_after[j +: 4] = nib;
  end
  assign qp0 = P[ra];
  assign qp1 = p_after;
  assign qo  = O[ra];
  assign qf  = f_after;
  assign qn  = (we0 | we1 | we2) ? 16'd0 : N[ra];
  assign qr  = r_after;
endmodule
