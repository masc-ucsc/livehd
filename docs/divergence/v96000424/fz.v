module fz(
  input clock,
  input reset,
  input [8:0] i0,
  input [4:0] i1,
  input [30:0] i2,
  output [6:0] o0,
  output signed [16:0] o1
);
  function automatic [15:0] f0(input signed [4:0] a0);
    f0 = ((~a0) >>> 7);
  endfunction
  reg signed [30:0] t0;
  always @(*) begin
    t0 = ($unsigned(4'hf) || (i0 ^ i1));
    for (int k = 0; k < 4; k = k + 1) t0[k] = t0[k] ^ ((-6'd10));
  end
  wire signed [15:0] t1 = i2;
  wire signed [15:0] t2 = 3;
  reg [16:0] t3;
  always @(*) begin
    t3 = (t0 ? (1'b1 << 0) : (t1 * t1));
    casez ({t1[0], i1[3]})
      2'b1?: t3 = ((t1 ^ i1) << 4);
      2'b01: t3 = ((i1 | 3'd3) * (i2 % (t1 | 1'b1)));
      default: ;
    endcase
  end
  reg [30:0] l0;
  always_latch begin
    if (clock && reset) l0 = 0;
    else if (clock && (|(&i2))) l0 = (!i2);
  end
  reg [8:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && ((i1 + i0))) l1 = i2[22];
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || ({t3[5:3], t1});
  wire gclk0 = clock & gen0;
  reg [4:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 29; else g0_0 <= (i1 - i1);
  assign o0 = ({3{5'd22}} == t0[26]);
  assign o1 = (l1 ? ((i0 ? g0_0 : 8) - (i1 & t3)) : (l0 + l1[3:3]));
endmodule
