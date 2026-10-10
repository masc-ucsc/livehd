module fz(
  input clock,
  input reset,
  input [30:0] i0,
  input [31:0] i1,
  input [6:0] i2,
  input [6:0] i3,
  input [16:0] i4,
  input signed [7:0] i5,
  output [31:0] o0,
  output [11:0] o1,
  output signed [47:0] o2,
  output signed [1:0] o3
);
  function automatic [32:0] f0(input [11:0] a0);
    f0 = (((a0 / (a0 | 1'b1)) / (4'd11 | 1'b1)) / (((a0 >> 2) || $signed(33'h7b3a8b8c)) | 1'b1));
  endfunction
  reg signed [64:0] t0;
  always @(posedge clock) if (reset) t0 <= 26571; else t0 <= (4'b0001 ? i0[9] : (i4[11:5] ** 1));
  wire signed [8:0] t1 = ((((i4 ^~ i0) / ((t0 / (i3 | 1'b1)) | 1'b1)) + i4[12]) ^~ ((i5 | i5[7 - i3[1:0] -: 1]) ? (8'd255 & (i1 + i3)) : {i0, t0}));
  wire [47:0] t2 = (&i2);
  wire [15:0] t3 = 4'h3;
  reg signed [3:0] t4;
  always @(posedge clock) begin
    if (reset) t4 <= 2;
    else if ((i3 >>> 3)) t4 <= t2;
  end
  reg [47:0] t5;
  always @(posedge clock) begin
    if (reset) t5 <= 42820;
    else if ((i1 >> 0)) t5 <= t1;
  end
  wire [31:0] t6 = i3[6:5];
  reg [3:0] l0;
  always_latch begin
    if (clock && reset) l0 = 0;
    else if (clock && ({i5[7:5], i3[6:6]})) l0 = i0[29:11];
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|i2[5:4]);
  wire gclk0 = clock & gen0;
  reg [11:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 1076; else g0_0 <= (|(!(i1 + t5)));
  reg [6:0] m1;
  reg [7:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((((t1 / (12'd1740 | 1'b1)) + i1) + {t3[4:1], t6, i2})) begin m1 <= (((i0 % (t5 | 1'b1)) ^ (0 >> t4)) | (i5[0] == 4'hb)); m2 <= m1; end
      else if (((i1 - {8'd231, 1'd1}) & ((t3 | t2) * (8'sd65 != i3)))) m2 <= t6[29:8];
      else m1 <= m2;
    end
  end
  assign o0 = t0[62];
  assign o1 = ((|((t6 ^ i5) * (3'sd2 * t4))) + {t3, 1'd1, t1[8:7]});
  assign o2 = t2[43:0];
  assign o3 = (((i3 + m2[i1[1:0] +: 5]) ** 1) << 7);
endmodule
