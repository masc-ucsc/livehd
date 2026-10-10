module fz(
  input clock,
  input reset,
  input [15:0] i0,
  input [31:0] i1,
  output signed [15:0] o0,
  output o1,
  output [8:0] o2
);
  function automatic f0(input [6:0] a0, input [4:0] a1);
    f0 = (0 >> 4);
  endfunction
  function automatic [16:0] f1(input [30:0] a0);
    f1 = ((a0 ? a0 : a0) >> 3);
  endfunction
  wire signed [4:0] t0 = (((i1 ^~ 4) ^~ (i1 << 2)) ^~ i0[i0[2:0]]);
  wire [16:0] t1 = (({t0[2:2], 8'd243} | (33'd2236644611 | i0)) >> t0);
  reg signed [3:0] t2;
  always @(posedge clock) if (reset) t2 <= 14; else t2 <= (16'sd30919 + i0[11:10]);
  wire t3 = (((i0 << t2) + (12'sd28 <<< 1)) ^ ((t0 - i0) <= (i0 - t1)));
  reg [6:0] l0;
  always @(*) if (clock && (reset || ((i0 * i1)))) l0 = reset ? 0 : t1[11:7];
  reg [3:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|t1[5])) l1 = t0;
  end
  reg [3:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + (l1 * (i1 >= 16'sd8344));
  reg [7:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (i1[28]) mem[i1[1:0]] <= f1((t2 << 6));
  end
  wire [7:0] mrd = mem[i1[1:0]];
  wire [4:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(3'd4), .b(mrd), .y(sub_o));
  assign o0 = ((t3 >>> t0) <<< t0);
  assign o1 = (t2[0] >> fb);
  assign o2 = (((i1 >= t1) ? (8'sd14 << 5) : (t2 * i1)) - ((l1 ^ 1'd0) >>> 0));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [4:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a ^ b;
endmodule
