// Golden for mem_partial_write_mixed: nonblocking slice writes of the same
// cycle, in program order. Each touches only its own bits and a later one wins
// where they overlap; the reads see the committed contents.
module mem_partial_write_mixed (
  input         clock,
  input         we,
  input  [1:0]  a,
  input  [1:0]  b,
  input  [1:0]  pa,
  input  [7:0]  x,
  input  [7:0]  y,
  input  [1:0]  ra,
  output [15:0] q
);
  reg  [1:0]  ptr[3:0];
  reg  [15:0] mem[3:0];
  wire [1:0]  p = ptr[a];
  wire [1:0]  n = b + 2'd1;
  wire [1:0]  s = a ^ b;

  always @(posedge clock) begin
    ptr[pa] <= b;
    if (we) begin
      mem[p][7:0]  <= x;
      mem[p][15:8] <= y;
      mem[n][3:0]  <= y[3:0];
      mem[n][7:4]  <= x[7:4];
      mem[s][10:5] <= x[7:2];
    end
  end

  assign q = mem[ra];
endmodule
