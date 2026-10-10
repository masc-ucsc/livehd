module fz(
  input clock,
  input reset,
  input signed [1:0] i0,
  input [8:0] i1,
  input signed [1:0] i2,
  input signed [69:0] i3,
  output [6:0] o0,
  output [2:0] o1,
  output [15:0] o2,
  output [16:0] o3
);
  function automatic [63:0] f0(input [1:0] a0, input signed [47:0] a1);
    f0 = ($unsigned((16'h979a - a0)) / (4'h2 | 1'b1));
  endfunction
  function automatic [30:0] f1(input signed [11:0] a0, input [63:0] a1);
    f1 = {4'd13, a1};
  endfunction
  reg [7:0] t0;
  always @(posedge clock) begin
    if (reset) t0 <= 144;
    else if (((i1 > i3) < i0[0:0])) t0 <= $signed((i2[0] ? ((i3 % (i3 | 1'b1)) ^~ (7 || i0)) : (i2 ^~ i1)));
  end
  wire signed [16:0] t1 = (|i3);
  reg [1:0] t2;
  always @(posedge clock) begin
    if (reset) t2 <= 2;
    else if (t1) t2 <= ($signed((|(i2 + 8'sd108))) - ((f0(6, t1) | (t0 / (9 | 1'b1))) ^~ t0[7:7]));
  end
  wire [7:0] t3 = (({i1, t2[1:1]} & i2) | i3[56:32]);
  reg [8:0] t4;
  always @(posedge clock) if (reset) t4 <= 438; else t4 <= t1[7];
  reg [15:0] l0;
  always_latch begin
    if (clock && reset) l0 = 0;
    else if (clock && ({2'd3, i1[5:5], i2[1:0]})) l0 = (1'd0 > t0);
  end
  reg signed [15:0] l1;
  always @(*) if (!clock && (reset || (|(^(l0 * 5))))) l1 = reset ? 0 : t4;
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|(t1[t4[3:0]] - (i3 ? t1 : 5)));
  wire gclk0 = clock & gen0;
  reg [11:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 481; else g0_0 <= (((3 - 0) << 2) * 16'd20754);
  reg g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 1; else g0_1 <= (|t4);
  reg gen1;
  always @(*) if (!clock) gen1 = reset || (t4[1]);
  wire gclk1 = clock & gen1;
  reg [8:0] g1_0;
  always @(posedge gclk1) if (reset) g1_0 <= 4; else g1_0 <= ((i0 ? {t4, t2[1:1], t2[1:1]} : (12'd3822 / (l1 | 1'b1))) / (((t0 * g0_1) >= (i3 + t3)) | 1'b1));
  reg [11:0] mem [0:3];
  integer mi;
  always @(posedge clock) begin
    if (reset) begin
      for (mi = 0; mi < 4; mi = mi + 1) mem[mi] <= 0;
    end else if ((((2 >>> 5) / (2'h1 | 1'b1)) | t4)) mem[t0[1:0]] <= (((t3 << 8) ? g1_0 : $signed(1'b0)) ** 2);
  end
  wire [11:0] mrd = mem[i3[1:0]];
  reg [69:0] m1;
  reg [2:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((i3 & (i2 ^~ mrd)) - ((t1 ? i0 : g0_1) <= (g0_0 ^ t2)))) begin m1 <= t1; m2 <= m1; end
      else if (i1) m2 <= (|{i2, i0});
      else m1 <= m2;
    end
  end
  assign o0 = (g0_0 >>> g0_1);
  assign o1 = (($signed((m2 & mrd)) >>> m2) - i3);
  assign o2 = (i1[8 - l1[2:0] -: 1] > (((i2 ? mrd : t4) == (~33'd1418652045)) ? 2 : $unsigned((i3 / (g0_1 | 1'b1)))));
  assign o3 = ((((g0_1 - 3'h2) ? $signed(m2) : 3'h5) ? (t3 ^ i3) : ((&l0) ? i3[g0_0[5:0] +: 5] : (t0 ^ g0_0))) >>> m2);
endmodule
