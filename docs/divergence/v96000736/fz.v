module fz(
  input clock,
  input reset,
  input signed i0,
  input signed [3:0] i1,
  output signed [31:0] o0,
  output [3:0] o1
);
  function automatic [16:0] f0(input signed a0);
    f0 = a0;
  endfunction
  reg signed [15:0] t0;
  always @(posedge clock) if (reset) t0 <= 1827; else t0 <= (~^(4'd8 - (3'h6 | i0)));
  wire signed [2:0] t1 = ({3{t0}} == ((3'sd0 <<< 5) >= i1));
  wire signed [3:0] t2 = t1;
  wire signed [8:0] t3 = t2;
  reg [8:0] t4;
  always @(*) begin
    t4 = $signed((t2 <= t1));
    casez ({i0, i0})
      2'b1?: t4 = t3;
      2'b01: t4 = t2;
      default: ;
    endcase
  end
  reg signed [30:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|t2)) l0 = {2{i0}};
  end
  reg [6:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && ((t0 << i1))) l1 = $signed(t3);
  end
  reg l2;
  always @(*) if (!clock && (reset || (|(i0 ? t4 : 2'd3)))) l2 = reset ? 0 : 8'd21;
  reg gen0;
  always @(*) if (!clock) gen0 = reset || ((3 | t1));
  wire gclk0 = clock & gen0;
  reg signed [30:0] g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 15588; else g0_0 <= $unsigned(2'd2);
  reg [16:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 27058; else g0_1 <= t0;
  reg [30:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + g0_1;
  wire [4:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(1'd1), .b(t4), .y(sub_o));
  assign o0 = 3'd7;
  assign o1 = l2;
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [4:0] y);
  assign y = a ^ b;
endmodule
