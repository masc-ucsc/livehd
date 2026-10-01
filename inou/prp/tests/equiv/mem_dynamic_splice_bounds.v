// Golden for mem_dynamic_splice_bounds: runtime part selects whose positions
// reach past the word. Verilog writes only the bits of a part select that are
// inside the word: `b - 1` is a 32-bit expression, so b = 0 wraps far past the
// top; `b -: 4` runs below bit 0 when b < 3; `bu +: 4` on the ascending
// `[0:15]` word runs past index 15 when bu > 12.
module mem_dynamic_splice_bounds (
  input         clock,
  input         wf,
  input         wb,
  input  [1:0]  a,
  input  [15:0] d,
  input  [3:0]  b,
  input  [4:0]  bu,
  input         x,
  input  [3:0]  v,
  input  [1:0]  ra,
  output [15:0] qw,
  output [15:0] qn,
  output [15:0] qu
);
  reg [15:0] mw[3:0];
  reg [15:0] mn[3:0];
  reg [0:15] mu[3:0];

  always @(posedge clock) begin
    if (wf) begin
      mw[a] <= d;
      mn[a] <= d;
      mu[a] <= d;
    end
    if (wb) begin
      mw[a][b - 1]      <= x;
      mw[a][b - 2 +: 2] <= v[1:0];
      mn[a][b -: 4]     <= v;
      mu[a][bu +: 4]    <= v;
    end
  end

  assign qw = mw[ra];
  assign qn = mn[ra];
  assign qu = mu[ra];
endmodule
