module fz(
  input clock,
  input reset,
  input [16:0] i0,
  input [6:0] i1,
  output signed [15:0] o0,
  output [30:0] o1
);
  reg [8:0] t0;
  always @(posedge clock) begin
    if (reset) t0 <= 101;
    else if (($unsigned(i1) ^ (i0 - i1))) t0 <= i1[6:6];
  end
  reg [15:0] t1;
  always @(posedge clock) begin
    if (reset) t1 <= 10773;
    else if ((t0 ^~ i1)) t1 <= t0;
  end
  reg signed [1:0] t2;
  always @(*) begin
    t2 = {t1, i0};
    for (int k = 0; k < 2; k = k + 1) t2[k] = t2[k] ^ ((i0 ^~ 7));
  end
  reg [15:0] l0;
  always @(*) if (!clock && (reset || (t2))) l0 = reset ? 0 : i1[5:4];
  reg l1;
  always @(*) if (!clock && (reset || (|t0))) l1 = reset ? 0 : (8'b00000110 >>> 3);
  reg [15:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ((l1 ** 1)) mem[t2[1:0]] <= ((2'sd0 ? t0 : 2'd3) == (t0 ** 0));
  end
  wire [15:0] mrd = mem[i0[1:0]];
  wire [6:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(l0), .b(t2), .y(sub_o));
  assign o0 = (((2'd1 - 6'd25) != (i0 << 2)) - (33'sd3501064203 - (3'sd2 * sub_o)));
  assign o1 = (i0 ? mrd : ((sub_o < t0) == {sub_o, t2}));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [6:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a + b;
endmodule
