module fz(
  input clock,
  input reset,
  input [69:0] i0,
  input signed [69:0] i1,
  input signed [15:0] i2,
  input [4:0] i3,
  output [69:0] o0,
  output signed [64:0] o1
);
  function automatic [2:0] f0(input signed [2:0] a0, input signed [30:0] a1);
    f0 = (((a1 / (4'b0100 | 1'b1)) ^ (a0 >>> a0)) * (a1[a1[3:0]] & a1));
  endfunction
  wire signed [63:0] t0 = 6;
  reg [64:0] t1;
  always @(posedge clock) if (reset) t1 <= 28852; else t1 <= (((i1 + (16'd24892 - i1)) ? ((i0 % (i2 | 1'b1)) ? 6 : i0[13]) : i3) ? 12'd213 : (((12'd1571 / (i2 | 1'b1)) - (t0 <<< 0)) % ((t0 ? (i1 ? 1'h1 : i1) : i3) | 1'b1)));
  wire signed [2:0] t2 = ((((i2 | t0) - (4'b1011 ? 9 : i3)) ? (~|(i1 / (0 | 1'b1))) : ($signed(i1) >> i3)) - t0);
  wire [15:0] t3 = ({t0[14:10], i3, t2} ? (-((t2 ? i3 : 1'd0) + (i1 >> i3))) : t0);
  reg l0;
  always_latch begin
    if (!clock && reset) l0 = 0;
    else if (!clock && (|((2'b10 >>> 1) & {1{i3[1:0]}}))) l0 = ($unsigned((i0 * t3)) <<< 7);
  end
  reg l1;
  always @(*) if (clock && (reset || (i0))) l1 = reset ? 0 : t2;
  reg [32:0] l2;
  always_latch begin
    if (clock && reset) l2 = 0;
    else if (clock && (|i1)) l2 = t1;
  end
  localparam [6:0] P0 = 33'h91ac786d;
  assign o0 = ({l0, i3[3:3]} ? {i3, t2} : (3'd2 && ((t2 >> 7) >= (P0 ? l0 : t0))));
  assign o1 = t1;
endmodule
