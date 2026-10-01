// Golden for mem_bulk_vs_entry_order: every write of a cycle is a nonblocking
// assignment in program order, so the LAST write to an entry wins, a
// whole-array clear or load included, and a slice write lands on whatever the
// earlier writes of the cycle left. The reset restores `rl` and `cp`; a write
// during reset does not land. `m1`/`m2`/`m3` (program ordering) see the writes
// before them in program order; `f` (fwd) sees the entry as the whole cycle
// leaves it; every other read sees the committed contents.
module mem_bulk_vs_entry_order (
  input         clock,
  input         reset,
  input         wf,
  input         clr,
  input         we,
  input         ld,
  input  [1:0]  a,
  input  [15:0] d,
  input  [7:0]  x,
  input  [63:0] z,
  input  [1:0]  ra,
  output [15:0] ec_q,
  output [15:0] ce_q,
  output [15:0] rl_q,
  output [15:0] pc_q,
  output [15:0] cp_q,
  output [15:0] m1,
  output [15:0] m2,
  output [15:0] m3,
  output [15:0] f
);
  reg [15:0] ec[3:0];
  reg [15:0] ce[3:0];
  reg [15:0] rl[3:0];
  reg [15:0] pc[3:0];
  reg [15:0] cp[3:0];
  reg [15:0] pr[3:0];
  reg [15:0] pf[3:0];
  integer    i;

  always @(posedge clock) begin
    if (wf) ec[a] <= d;
    if (clr) for (i = 0; i < 4; i = i + 1) ec[i] <= 16'd0;

    if (clr) for (i = 0; i < 4; i = i + 1) ce[i] <= 16'd0;
    if (wf) ce[a] <= d;

    if (wf) pc[a] <= d;
    if (we) pc[a][7:0] <= x;
    if (clr) for (i = 0; i < 4; i = i + 1) pc[i] <= 16'd0;

    if (wf) begin
      pr[a] <= d;
      pf[a] <= d;
    end
    if (clr) begin
      for (i = 0; i < 4; i = i + 1) begin
        pr[i] <= 16'd0;
        pf[i] <= 16'd0;
      end
    end
    if (we) begin
      pr[a][15:8] <= x;
      pf[a][15:8] <= x;
    end

    if (reset) begin
      for (i = 0; i < 4; i = i + 1) begin
        rl[i] <= 16'd0;
        cp[i] <= 16'd0;
      end
    end else begin
      if (wf) rl[a] <= d;
      if (ld) for (i = 0; i < 4; i = i + 1) rl[i] <= z[i*16 +: 16];
      if (we) rl[ra] <= {8'd0, x};

      if (wf) cp[a] <= d;
      if (clr) for (i = 0; i < 4; i = i + 1) cp[i] <= 16'd0;
      if (we) cp[a][15:8] <= x;
    end
  end

  assign ec_q = ec[ra];
  assign ce_q = ce[ra];
  assign rl_q = rl[ra];
  assign pc_q = pc[ra];
  assign cp_q = cp[ra];

  // Program order: the entry as the writes before each read left it.
  wire [15:0] p1 = (wf && a == ra) ? d : pr[ra];
  wire [15:0] p2 = clr ? 16'd0 : p1;
  wire [15:0] p3 = (we && a == ra) ? {x, p2[7:0]} : p2;
  assign m1 = p1;
  assign m2 = p2;
  assign m3 = p3;

  // fwd: the entry as the whole cycle leaves it.
  wire [15:0] f1 = (wf && a == ra) ? d : pf[ra];
  wire [15:0] f2 = clr ? 16'd0 : f1;
  assign f = (we && a == ra) ? {x, f2[7:0]} : f2;
endmodule
