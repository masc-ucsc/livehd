// Golden for mem_replay_crossref.prp: Verilog nonblocking writes, with the
// Pyrope program-order reads spelled out.
module mem_replay_crossref(input clock, input w, input clr, input p, input [1:0] a, input [1:0] ra,
                           input [1:0] rb, input [2:0] x, output [5:0] t, output [5:0] r, output [5:0] q,
                           output [5:0] qd);
  reg [1:0] ix[0:3];
  reg [5:0] m[0:3];
  reg [5:0] n[0:3];
  reg       vl[0:3];
  reg [5:0] dt[0:3];
  integer i;
  wire [5:0] tv = clr ? 6'h11 : m[ra];
  wire [1:0] id = clr ? 2'd1 : ix[ra];
  wire       g  = clr ? 1'b0 : ((w && a == ra) ? 1'b1 : vl[ra]);
  // the entries the partial writes merge into, as the earlier writes left them
  wire [5:0] nb = w ? tv : n[id];
  wire [5:0] db = g ? tv : dt[ra];
  always @(posedge clock) begin
    if (w) vl[a] <= 1'b1;
    if (clr) for (i = 0; i < 4; i = i + 1) begin
      ix[i] <= 2'd1;
      m[i]  <= 6'h11;
      vl[i] <= 1'b0;
    end
    if (w) begin
      m[a]  <= tv;
      n[id] <= tv;
    end
    if (p) n[id] <= {nb[5:3], x};
    if (g) dt[ra] <= tv;
    if (p) dt[ra] <= {db[5:3], x};
  end
  assign t  = tv;
  wire [5:0] r1 = clr ? 6'h11 : m[rb];
  assign r  = (w && a == rb) ? tv : r1;
  assign q  = n[rb];
  assign qd = dt[rb];
endmodule
