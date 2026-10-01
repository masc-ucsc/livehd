// Golden for mem_dynamic_splice_after_write, and the Verilog side of the bug:
// every write of a cycle is a nonblocking assignment in program order, so a
// part select at a RUNTIME position lands on whatever the earlier writes of
// the cycle left in the word. The slang reader spliced a dynamic position with
// plain bit logic around a read of the COMMITTED word, so a full (`mf`) or
// chunked (`mc`) write of the same word earlier in the cycle was silently
// lost, and a second partial write (`ms`) was refused outright. `mr` and `me`
// are the common exclusive shapes (a reset or clear arm, else a dynamic write).
module mem_dynamic_splice_after_write (
  input         clock,
  input         reset,
  input         wf,
  input         wc,
  input         wb,
  input         ws,
  input         clr1,
  input  [1:0]  a,
  input  [1:0]  j,
  input  [15:0] d,
  input  [3:0]  b,
  input  [1:0]  c,
  input  [2:0]  e,
  input         x,
  input  [3:0]  v,
  input  [1:0]  ra,
  output [15:0] qf,
  output [15:0] qc,
  output [15:0] qs,
  output [15:0] qr,
  output [15:0] qe
);
  reg [15:0] mr[3:0];
  reg [15:0] mf[3:0];
  reg [15:0] mc[3:0];
  reg [15:0] ms[3:0];
  reg [15:0] me[3:0];
  integer    i;

  always @(posedge clock) begin
    if (wf) mf[a] <= d;
    if (wb) mf[a][b] <= x;

    if (wc) mc[a][15:8] <= d[15:8];
    if (wb) mc[a][c*4 +: 4] <= v;

    if (ws) ms[a][b] <= x;
    if (wb) ms[a][e +: 4] <= v;

    if (reset) begin
      for (i = 0; i < 4; i = i + 1) mr[i] <= 16'd0;
    end else if (wb) begin
      mr[a][b] <= x;
    end

    if (clr1) begin
      me[j] <= 16'd0;
    end else if (wb) begin
      me[a][b] <= x;
    end
  end

  assign qf = mf[ra];
  assign qc = mc[ra];
  assign qs = ms[ra];
  assign qr = mr[ra];
  assign qe = me[ra];
endmodule
