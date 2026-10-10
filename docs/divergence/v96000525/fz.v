module fz(
  input clock,
  input reset,
  input [4:0] i0,
  output [1:0] o0,
  output signed [8:0] o1
);
  function automatic f0(input signed [1:0] a0);
    f0 = (a0 || 16'd55103);
  endfunction
  function automatic [7:0] f1(input a0, input [7:0] a1);
    f1 = (a0 + a1);
  endfunction
  wire [4:0] t0 = ((7 >>> i0) > (i0 << 6));
  reg signed [7:0] t1;
  always @(*) begin
    t1 = t0;
    casez ({i0[1], i0[0]})
      2'b1?: t1 = (i0 ? i0 : 8'b00100001);
      2'b01: t1 = (i0 || 1'd1);
      default: ;
    endcase
  end
  wire [3:0] t2 = i0[0:0];
  reg [4:0] l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|t2)) l0 = t0;
  end
  reg signed [7:0] l1;
  always_latch begin
    if (!clock && reset) l1 = 0;
    else if (!clock && (|t2)) l1 = i0;
  end
  reg gen0;
  always @(*) if (!clock) gen0 = reset || (|t2);
  wire gclk0 = clock & gen0;
  reg g0_0;
  always @(posedge gclk0) if (reset) g0_0 <= 1; else g0_0 <= t0;
  reg [2:0] g0_1;
  always @(posedge gclk0) if (reset) g0_1 <= 5; else g0_1 <= t1;
  assign o0 = l1;
  assign o1 = t0;
endmodule
