// Golden for mem_partial_write_update: nonblocking writes in program order.
// Slice writes of one entry all land (a later one wins where they overlap),
// and the lane writes of `mu` land over its same-cycle clear. `q` and `u` read
// the committed contents; `f` (program ordering) forwards the last write of
// this cycle that hits its address. The reset restores `mr` and `mp`, and a
// write during reset neither lands nor forwards.
module mem_partial_write_update (
  input         clock,
  input         reset,
  input         we,
  input         clr,
  input  [1:0]  a,
  input  [31:0] d,
  input  [3:0]  be,
  input         wf,
  input  [1:0]  b,
  input  [31:0] e,
  input  [1:0]  ra,
  output [31:0] q,
  output [31:0] u,
  output [31:0] f
);
  reg [31:0] mr[3:0];
  reg [31:0] mu[3:0];
  reg [31:0] mp[3:0];
  integer    i;

  always @(posedge clock) begin
    if (clr) begin
      for (i = 0; i < 4; i = i + 1) mu[i] <= 32'd0;
    end
    if (we) begin
      if (be[0]) mu[a][7:0] <= d[7:0];
      if (be[1]) mu[a][15:8] <= d[15:8];
      if (be[2]) mu[a][23:16] <= d[23:16];
      if (be[3]) mu[a][31:24] <= d[31:24];
    end
    if (reset) begin
      for (i = 0; i < 4; i = i + 1) begin
        mr[i] <= 32'd0;
        mp[i] <= 32'd0;
      end
    end else begin
      if (we) begin
        if (be[0]) mr[a][7:0] <= d[7:0];
        if (be[1]) mr[a][15:8] <= d[15:8];
        if (be[2]) mr[a][23:16] <= d[23:16];
        if (be[3]) mr[a][31:24] <= d[31:24];
        mp[a] <= d;
      end
      if (wf) mp[b] <= e;
    end
  end

  assign q = mr[ra];
  assign u = mu[ra];
  assign f = (!reset && wf && b == ra) ? e : (!reset && we && a == ra) ? d : mp[ra];
endmodule
