module fz(
  input clock,
  input reset,
  input signed [2:0] i0,
  input [8:0] i1,
  output [6:0] o0,
  output [31:0] o1,
  output o2
);
  function automatic [2:0] f0(input signed [2:0] a0);
    f0 = ((~|a0) / (a0[2:0] | 1'b1));
  endfunction
  reg [11:0] t0;
  always @(*) begin
    t0 = 0;
    casez ({i0[2], i0[0]})
      2'b1?: t0 = (i1 < (i0 == i0));
      2'b01: t0 = i0;
      default: ;
    endcase
  end
  reg [7:0] l0;
  always @(*) if (clock && (reset || (|(i0 ^ i0)))) l0 = reset ? 0 : t0[i0[2:0] +: 5];
  reg [30:0] l1;
  always @(*) if (clock && (reset || (i0))) l1 = reset ? 0 : t0[6:6];
  reg signed [16:0] l2;
  always_latch begin
    if (!clock && reset) l2 = 0;
    else if (!clock && (|t0[1:0])) l2 = (2'b11 ? (i0 % (l1 | 1'b1)) : (~^l1));
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (f0(i0));
  wire gclk0 = clock & gen0;
  reg signed [4:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 29; else g0_0 <= f0((5 >> i0));
  reg [11:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 2820; else g0_1 <= (i1 ? t0[10:3] : {7'd105, t0});
  localparam [7:0] P0 = 2'b11;
  reg fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + ((l2 / (g0_0 | 1'b1)) - (l2 ^~ i1));
  wire [16:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(i0[2:0]), .b(l2), .y(sub_o));
  assign o0 = (sub_o[12:6] + (|{g0_0, l1}));
  assign o1 = 12'hf9f;
  assign o2 = $signed((fb / ((3'h3 - l0) | 1'b1)));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [16:0] y);
  assign y = a ^ b;
endmodule
