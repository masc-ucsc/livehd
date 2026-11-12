// :test: lec_no_x
// A variable with a declaration initializer that nothing drives holds that
// value: it is a constant, not an unknown. LiteX emits one such `reg` per
// disabled feature (`reg replace = 1'd0;`), read by write enables and level
// counters; as an unknown it let synthesis tie litedram's FIFO write enables
// off. A driven variable keeps its initializer as a power-on value instead.
module undriven_var_init (
    input  wire       clk,
    input  wire [7:0] a,
    input  wire       sel,
    output reg  [7:0] q,
    output wire [7:0] y,
    output wire [3:0] z,
    output wire       w,
    output wire       we
);
  reg              replace = 1'd0;
  reg              en      = 1'd1;
  reg        [7:0] mask    = 8'h3c;
  reg signed [3:0] neg     = -4'sd2;
  reg              used    = 1'd0;  // driven below: a register with a power-on value

  always @(posedge clk) used <= sel;

  // The first read of `en` sits inside a branch arm.
  always @(*) begin
    q = 8'd0;
    if (sel) begin
      if (en) q = a & mask;
    end else begin
      q = a | mask;
    end
  end

  assign y  = used ? a : (a ^ mask);
  assign z  = neg;
  assign we = a[7] & (a[6] | replace);

  genvar i;
  generate
    for (i = 0; i < 2; i = i + 1) begin : g
      reg  gk = 1'd1;  // one per generate iteration
      wire t = a[i] & gk & ~replace;
    end
  endgenerate
  assign w = g[0].t ^ g[1].t;
endmodule
