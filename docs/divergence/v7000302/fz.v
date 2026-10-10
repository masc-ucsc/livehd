module fz(
  input clock,
  input reset,
  input [47:0] i0,
  input [69:0] i1,
  input [3:0] i2,
  input [1:0] i3,
  output [15:0] o0,
  output [4:0] o1
);
  wire [3:0] t0 = (6'h1a ? i2[2:0] : i0);
  reg [69:0] t1;
  always @(posedge clock) begin
    if (reset) t1 <= 59732;
    else if ((((i3 ? t0 : t0) << 3) >= ((i2 << 4) ^ (6'h33 >>> 2)))) t1 <= ($unsigned((i0[1] < {3{i2}})) ^ i0);
  end
  wire signed [47:0] t2 = ((^(~^(t1 % (t1 | 1'b1)))) % (($unsigned((33'hc19baae6 | t0)) >= $signed({3{t1[56:45]}})) | 1'b1));
  reg signed [69:0] t3;
  always @(posedge clock) if (reset) t3 <= 63199; else t3 <= (t1 ? t2[20] : 16'd51614);
  wire signed [1:0] t4 = i2;
  reg [6:0] t5;
  always @(posedge clock) if (reset) t5 <= 41; else t5 <= i2;
  reg [11:0] t6;
  always @(*) begin
    t6 = (($unsigned(t3) + t4) != 4'sd3);
    if (t5) t6 = ($signed(0) & i3);
  end
  reg signed [63:0] t7;
  always @(posedge clock) begin
    if (reset) t7 <= 514;
    else if ((t6 < (~(1'd1 >> i2)))) t7 <= 16'b0000111110101010;
  end
  localparam [8:0] P0 = 8'd35;
  reg signed [3:0] m1;
  reg [32:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((16'b0110011100001010 <<< 1) | ({P0, t0, i0[16:10]} ? (1'd0 * i0) : i3[0:0]))) begin m1 <= t1[51:1]; m2 <= m1; end
      else if (t0) m2 <= ($signed((P0 - t1)) ? ((6'h2f - i2) ^ i3) : ((i0 & i1) / ((16'h6178 & t2) | 1'b1)));
      else m1 <= m2;
    end
  end
  assign o0 = (!(($signed(t3) ? t4 : (t5 - t4)) ^~ m1[3:2]));
  assign o1 = (m2 / ((m1 + ((i3 + i1) / (t4[1] | 1'b1))) | 1'b1));
endmodule
