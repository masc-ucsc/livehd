module fz(
  input clock,
  input reset,
  input [4:0] i0,
  input signed [1:0] i1,
  output signed [8:0] o0,
  output [16:0] o1,
  output signed [3:0] o2
);
  function automatic [1:0] f0(input a0);
    f0 = a0;
  endfunction
  function automatic [16:0] f1(input [3:0] a0, input [11:0] a1);
    f1 = a0;
  endfunction
  wire signed [1:0] t0 = (({2{i0}} / (f1(i0, 16'b0111101110110011) | 1'b1)) - i1);
  reg signed [1:0] t1;
  always @(posedge clock) if (reset) t1 <= 3; else t1 <= ((~&i1) % (i0 | 1'b1));
  reg signed [7:0] t2;
  always @(posedge clock) begin
    if (reset) t2 <= 29;
    else if (((t0 >> 0) ? {i0, t1} : 3'd2)) t2 <= (4'h4 & ((i0 - t1) & 2'h3));
  end
  reg [6:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 111;
    else if ({t1[0:0], t0}) t3 <= t1[0];
  end
  reg [6:0] l0;
  always @(*) if (clock && (reset || (|i1[0]))) l0 = reset ? 0 : (i1 ? t1[1:1] : t0);
  reg signed [15:0] l1;
  always_latch begin
    if (clock && reset) l1 = 0;
    else if (clock && (|(i1 * i0))) l1 = ((~&i1) & t1[1]);
  end
  reg signed [4:0] l2;
  always_latch begin
    if (clock && reset) l2 = 0;
    else if (clock && ((5 % (i1 | 1'b1)))) l2 = {2{4'd13}};
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || ((t1 - t3));
  wire gclk0 = clock & gen0;
  reg signed [11:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 1308; else g0_0 <= (f1(i1, t3) != (t3 % (i1 | 1'b1)));
  reg [4:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 25; else g0_1 <= g0_0;
  localparam P0 = 2'h0;
  reg signed [8:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + {i0, 1'd1, 2'd1};
  reg signed [31:0] m1;
  reg [16:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if ({i0, g0_1}) begin m1 <= ((t2 ^~ P0) ** 2); m2 <= m1; end
      else if ((fb + g0_0[2])) m2 <= ((33'd2091582947 << i0) - (~|33'd4733334065));
      else m1 <= m2;
    end
  end
  assign o0 = (((m2 % (m2 | 1'b1)) + (t3 | 6'h2b)) >> t1);
  assign o1 = (({t0[1:1], 1'd1} >> i0) + P0);
  assign o2 = {2{m2[16:14]}};
endmodule
