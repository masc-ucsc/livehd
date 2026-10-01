// Memory `initial` contents the Verilog reader must place by ENTRY and keep at
// full element width: `mem [1:4]` fills entry 0 from `mem[1]` (every access
// addresses `index - 1`), and a 128-bit element keeps an unsigned value with
// bit 63 set, and one of 2^64, exactly (an int64 harvest sign-extended the
// first and dropped the second). The contents are power-on only (no reset).
module verilog_mem_init_bounds (
  input          clk,
  input          we,
  input  [  1:0] wa,
  input  [  1:0] ra,
  input  [  7:0] d,
  input  [127:0] dw,
  output [  7:0] q,
  output [127:0] qw
);

  reg [  7:0] mem  [1:4];
  reg [127:0] wide [0:3];

  initial begin
    mem[1]  = 8'd10;
    mem[2]  = 8'd20;
    mem[3]  = 8'd30;
    mem[4]  = 8'd40;
    wide[0] = 128'hFFFF_FFFF_FFFF_FFFF;
    wide[1] = 128'h1;
    wide[2] = 128'h8000_0000_0000_0000;
    wide[3] = 128'h1_0000_0000_0000_0000;
  end

  always @(posedge clk) begin
    if (we) begin
      mem[wa+1] <= d;
      wide[wa]  <= dw;
    end
  end

  assign q  = mem[ra+1];
  assign qw = wide[ra];

endmodule
