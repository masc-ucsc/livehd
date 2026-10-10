module fz(
  input clock,
  input reset,
  input [2:0] i0,
  input [7:0] i1,
  input [11:0] i2,
  input signed [3:0] i3,
  input [31:0] i4,
  output signed [69:0] o0,
  output [32:0] o1,
  output [16:0] o2
);
  wire [47:0] t0 = (8 == ({i1[3:3], i4[18:11], i1} ^~ $signed((i4 >> i3))));
  reg [11:0] t1;
  always @(posedge clock) begin
    if (reset) t1 <= 1560;
    else if ((~i4[10])) t1 <= ($unsigned((i4 ? (2'sd0 + i1) : i3)) + 33'd5265320357);
  end
  reg [31:0] t2;
  always @(*) begin
    t2 = (i0[2] ^~ i3);
    casez ({i2[9], i3[3]})
      2'b1?: t2 = t0[45];
      2'b01: t2 = (i1 < i0[1:0]);
      default: ;
    endcase
  end
  reg [6:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 52;
    else if ({t2, i4[10:6]}) t3 <= {i3, i3[2:2]};
  end
  reg [47:0] t4;
  always @(*) begin
    t4 = (i4[7:4] >> i3);
    case (4'd1)
      1: t4 = (i2 >= {t0[23:15], i2, i1});
      2: t4 = (t3 ? ((t0 + i3) ^~ $unsigned(4)) : i4);
      default: ;
    endcase
  end
  reg signed [4:0] t5;
  always @(*) begin
    t5 = ((7 - i4) ** 3);
    if ((2 >> i0)) t5 = 8'ha4;
  end
  reg [11:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|((~&t2) >> t5))) l0 = (((~t3) != t5) < (i1[1] ? (33'b111011100000110010010001000100001 ? i4 : t3) : t0[26]));
  end
  reg [4:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && (|16'd32416)) l1 = ($unsigned((i3 | t5)) ? ((t5 ? i1 : l0) << i0) : i2);
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (t0);
  wire gclk0 = clock & gen0;
  reg [6:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 87; else g0_0 <= (t2[t4[3:0]] % (((i4 % (i1 | 1'b1)) ? (t5 * t0) : 2) | 1'b1));
  reg [64:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 49329; else g0_1 <= (i0[2:1] & i2);
  assign o0 = (l0 & t5);
  assign o1 = ((($signed(t4) >>> 8) ^~ (4 ^~ 12'sd1147)) & ((i2 + (16'h2158 == 3'b000)) ^~ g0_1));
  assign o2 = (({3{t5[2:1]}} >> 0) == l0);
endmodule
