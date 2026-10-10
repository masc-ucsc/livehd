module fz(
  input clock,
  input reset,
  input [8:0] i0,
  input [31:0] i1,
  output [31:0] o0,
  output [15:0] o1,
  output [2:0] o2
);
  function automatic f0(input [7:0] a0, input [8:0] a1);
    f0 = a1;
  endfunction
  reg t0;
  always @(*) begin
    t0 = i1;
    casez ({i1[14], i1[10]})
      2'b1?: t0 = (i0[5:1] ^ (i1 ? i0 : i1));
      2'b01: t0 = ((i1 | i1) << 1);
      default: ;
    endcase
  end
  reg [8:0] t1;
  always @(*) begin
    t1 = ((i1 >> 3) || t0);
    casez ({i1[14], i1[3]})
      2'b1?: t1 = ((!t0) | (i1 ^ i0));
      2'b01: t1 = (i0 | (i0 ? i0 : i0));
      default: ;
    endcase
  end
  wire t2 = f0(t1[4:4], i1);
  wire [6:0] t3 = (t0 >> t0);
  wire signed [1:0] t4 = (((i0 << 0) < (t3 ^ t0)) ^~ t2);
  reg [16:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|(t4 ^~ t0))) l0 = (|(i1 + 33'd3677060109));
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|t3);
  wire gclk0 = clock & gen0;
  reg [31:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 4520; else g0_0 <= ((1'sd0 ? i0 : l0) >> t4);
  reg [6:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 98; else g0_1 <= i0[7:6];
  localparam [31:0] P0 = 4'd6;
  wire [0:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(t2), .b(i1[30:25]), .y(sub_o));
  assign o0 = (((P0 - t3) <= $signed(4'd9)) & ((t3 | t0) >>> sub_o));
  assign o1 = t1[8:7];
  assign o2 = l0;
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [0:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a + b;
endmodule
