// Golden for generic_mem_byte_lanes: per-lane nonblocking writes (all lanes of
// a cycle land); `old` reads the committed entry, `prog` reads the committed
// entry before the writes (qb) and the merged next value after them (qa).
module generic_mem_byte_lanes (
  input         clock,
  input         reset,
  input         we,
  input  [1:0]  a,
  input  [23:0] d,
  input  [2:0]  be,
  input  [1:0]  ra,
  output [15:0] qo2,
  output [15:0] qb2,
  output [15:0] qa2,
  output [23:0] qo3,
  output [23:0] qb3,
  output [23:0] qa3
);
  reg [15:0] o2[1:0];
  reg [15:0] p2[1:0];
  reg [23:0] o3[3:0];
  reg [23:0] p3[3:0];

  wire        w  = we && !reset;
  wire [15:0] n2 = {be[1] ? d[15:8] : p2[a[0]][15:8], be[0] ? d[7:0] : p2[a[0]][7:0]};
  wire [23:0] n3 = {be[2] ? d[23:16] : p3[a][23:16], be[1] ? d[15:8] : p3[a][15:8], be[0] ? d[7:0] : p3[a][7:0]};

  integer k;
  always @(posedge clock) begin
    if (reset) begin
      for (k = 0; k < 2; k = k + 1) begin o2[k] <= 16'd0; p2[k] <= 16'd0; end
      for (k = 0; k < 4; k = k + 1) begin o3[k] <= 24'd0; p3[k] <= 24'd0; end
    end else if (we) begin
      if (be[0]) o2[a[0]][7:0] <= d[7:0];
      if (be[1]) o2[a[0]][15:8] <= d[15:8];
      if (be[0]) o3[a][7:0] <= d[7:0];
      if (be[1]) o3[a][15:8] <= d[15:8];
      if (be[2]) o3[a][23:16] <= d[23:16];
      p2[a[0]] <= n2;
      p3[a] <= n3;
    end
  end

  assign qo2 = o2[ra[0]];
  assign qb2 = p2[ra[0]];
  assign qa2 = (w && a[0] == ra[0]) ? n2 : p2[ra[0]];
  assign qo3 = o3[ra];
  assign qb3 = p3[ra];
  assign qa3 = (w && a == ra) ? n3 : p3[ra];
endmodule
