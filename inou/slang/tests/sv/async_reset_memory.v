// Asynchronous memory resets the reader must extract, not demote to a
// synchronous reset. A demoted reset clears the memory only at the next clock
// edge, so the cycle the reset is asserted reads stale contents (and a reset
// pulse with no clock edge is lost). Two shapes:
//   m: active-low, a for loop gives every entry a value of its own;
//   n: active-high, a list of element assignments to a descending memory, in
//      the same rung as an ordinary register (c).
// Checked by tests/async_reset_memory_test.sh.
module async_reset_memory (
    input  logic        clk,
    input  logic        rst_n,
    input  logic        rst,
    input  logic        we,
    input  logic [1:0]  wa,
    input  logic [7:0]  wd,
    input  logic [1:0]  ra,
    output logic [7:0]  rd_m,
    output logic [3:0]  rd_n,
    output logic [31:0] all_m,
    output logic [15:0] all_n,
    output logic [7:0]  cnt
);
  logic [7:0] m[0:3];
  logic [3:0] n[3:0];
  logic [7:0] c;
  integer k;

  always @(posedge clk or negedge rst_n)
    if (!rst_n) begin
      for (k = 0; k < 4; k = k + 1) m[k] <= 8'h30 + 8'(k);
    end else if (we) begin
      m[wa] <= wd;
    end

  always_ff @(posedge clk or posedge rst)
    if (rst) begin
      n[0] <= 4'h1;
      n[1] <= 4'h2;
      n[2] <= 4'h4;
      n[3] <= 4'h8;
      c    <= 8'ha5;
    end else begin
      c <= c + 8'd1;
      if (we) n[wa] <= wd[3:0];
    end

  assign rd_m  = m[ra];
  assign rd_n  = n[ra];
  assign all_m = {m[3], m[2], m[1], m[0]};
  assign all_n = {n[3], n[2], n[1], n[0]};
  assign cnt   = c;
endmodule
