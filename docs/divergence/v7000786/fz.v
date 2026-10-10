module fz(
  input clock,
  input reset,
  input signed [1:0] i0,
  input [30:0] i1,
  output [11:0] o0,
  output signed [2:0] o1
);
  reg [3:0] t0;
  always @(*) begin
    t0 = (&(i0 | i0));
    case (3'd7)
      2: t0 = (i1 / ((i1 + 3'sd0) | 1'b1));
      default: ;
    endcase
  end
  reg [4:0] t1;
  always @(posedge clock) if (reset) t1 <= 3; else t1 <= t0;
  wire [16:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(6'd49), .b(i0[1:0]), .y(sub_o));
  reg signed [30:0] m1;
  reg [6:0] m2;
  always @(posedge clock) begin
    if (reset) begin m1 <= 1; m2 <= 0; end
    else begin
      if (((6'h32 & i1) & (i0 ^~ t1))) begin m1 <= t0; m2 <= m1; end
      else if (3'sd3) m2 <= (t1[1:0] << i0);
      else m1 <= m2;
    end
  end
  assign o0 = sub_o;
  assign o1 = ((12'd493 + i1) - ({m2[6:3], sub_o} <<< 8));
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output reg [16:0] y);
  always @(posedge clock) if (reset) y <= 0; else y <= a & b;
endmodule
