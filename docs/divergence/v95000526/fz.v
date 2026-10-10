module fz(
  input clock,
  input reset,
  input [7:0] i0,
  input signed [2:0] i1,
  input [8:0] i2,
  input [8:0] i3,
  input [31:0] i4,
  output signed [15:0] o0,
  output [30:0] o1
);
  wire [15:0] t0 = $unsigned(((i4 ? (&i2) : i1) * $unsigned(i0[1])));
  wire [3:0] t1 = (t0[5:4] + t0);
  reg [15:0] t2;
  always @(posedge clock) if (reset) t2 <= 62795; else t2 <= t1;
  reg signed [16:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 19795;
    else if (($signed((i0 != i1)) * i3)) t3 <= i4[13:2];
  end
  reg signed [4:0] t4;
  always @(*) begin
    t4 = {2'd0, i2[8:8]};
    casez ({i3[2], i3[7]})
      2'b1?: t4 = (((1'sd0 >= t3) / (1 | 1'b1)) + ({2{i4[30:24]}} / (t0 | 1'b1)));
      2'b01: t4 = i0[i0[1:0]];
      default: ;
    endcase
  end
  reg [15:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (t4[4:0])) l0 = {i0, i2[6:2], t4[4:4]};
  end
  reg [7:0] l1;
  always @(*) if (!clock && (reset || (|((i1 || t0) >> t4)))) l1 = reset ? 0 : (t4[3:0] >>> 7);
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (($unsigned(t1) * (t4 / (i0 | 1'b1))));
  wire gclk0 = clock & gen0;
  reg signed [32:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 45664; else g0_0 <= (((l1 ? i2 : 3) % ({1{i3}} | 1'b1)) | ((t2 - 12'd1785) << 4));
  reg [69:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 37119; else g0_1 <= (i4[31:29] % ((t4 >>> t4) | 1'b1));
  wire [31:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i0), .b(t2[15:15]), .y(sub_o));
  reg signed [7:0] m1;
  reg [63:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ((l1 + ((t4 >> t1) ? {3{l1}} : (i1 && 2'd3)))) begin m1 <= (2 + ((i2 >> 5) * i2[4])); m2 <= m1; end
      else if ($unsigned(((l0 / (t3 | 1'b1)) + (t0 ^ 16'd39976)))) m2 <= g0_0[t2[4:0] +: 1];
      else m1 <= m2;
    end
  end
  assign o0 = l0[11];
  assign o1 = ((8'b00111000 ? (12'sd1154 - 3'd4) : m1) + 8);
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [31:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a - b;
endmodule
