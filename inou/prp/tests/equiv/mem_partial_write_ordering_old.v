// Golden for mem_partial_write_ordering_old: nonblocking slice writes of one
// entry in the same cycle all land (each touches only its own bits; a later
// slice wins where they overlap), and the read sees the committed contents,
// never this cycle's writes.
module mem_partial_write_ordering_old (
  input         clock,
  input         we,
  input  [1:0]  a,
  input  [15:0] d,
  input  [1:0]  be,
  input         wb,
  input  [1:0]  b,
  input  [15:0] e,
  input  [1:0]  ra,
  output [15:0] q
);
  reg [15:0] mem[3:0];

  always @(posedge clock) begin
    if (we) begin
      if (be[0]) mem[a][7:0] <= d[7:0];
      if (be[1]) mem[a][15:8] <= d[15:8];
    end
    if (wb) mem[b][9:3] <= e[6:0] + 7'd1;
  end

  assign q = mem[ra];
endmodule
