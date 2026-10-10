module fz(
  input clock,
  input reset,
  input [69:0] i0,
  input [32:0] i1,
  input [8:0] i2,
  input signed [32:0] i3,
  output [69:0] o0,
  output [30:0] o1,
  output [32:0] o2,
  output signed [4:0] o3
);
  function automatic [1:0] f0(input signed [2:0] a0, input [2:0] a1);
    f0 = (((a0 <= a0) ? (a1 - a0) : a1) >>> 1);
  endfunction
  function automatic [8:0] f1(input signed [6:0] a0, input [8:0] a1);
    f1 = a0;
  endfunction
  reg t0;
  always @(*) begin
    t0 = {i3, 1'd0, i2};
    if (i0[65:57]) t0 = ((!(i0 <= i3)) ^ (-(i1 && i3)));
  end
  wire [15:0] t1 = (((i2[2] - i1) / (i3 | 1'b1)) ? (i3[18] ^ (i1 ^~ 12'sd363)) : (({2{i3[27:25]}} ** 2) >> 3));
  reg [15:0] t2;
  always @(*) begin
    t2 = ((i3[19] ^ (i3 - t0)) || (-(^i0)));
    for (int k = 0; k < 4; k = k + 1) t2[k] = t2[k] ^ ((2'd1 << t0));
  end
  wire [7:0] t3 = (({t0, t1[3:2]} - ({1'd1, t0} ? t1[1] : (i3 ? 9 : t2))) & (((i1 % (i0 | 1'b1)) * (8'd243 ^ i0)) >> 2));
  wire signed [64:0] t4 = {t1, i0[64:38], t2};
  wire [8:0] t5 = t4;
  reg signed [32:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if (((t0 != (^i0)) - ((8'd124 >> 7) & (i2 >= 8)))) mem[t2[1:0]] <= t5;
  end
  wire signed [32:0] mrd = mem[{1'b0, t0}];
  wire [6:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i0[69:28]), .b(t4[62:16]), .y(sub_o));
  assign o0 = (t0 + ((i2 ^~ f1(i0, i0)) ** 2));
  assign o1 = t3[t3[1:0]];
  assign o2 = (t3 & t2[6:6]);
  assign o3 = (~^((-(mrd & t1)) ? ((i3 >> 7) ? {4'd2, 7'd116} : mrd) : {3{t3}}));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [6:0] y);
  assign y = a + b;
endmodule
