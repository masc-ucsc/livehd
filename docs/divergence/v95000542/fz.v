module fz(
  input clock,
  input reset,
  input signed [6:0] i0,
  input signed [47:0] i1,
  input [32:0] i2,
  input i3,
  output [32:0] o0,
  output [4:0] o1,
  output [3:0] o2
);
  reg t0;
  always @(posedge clock) if (reset) t0 <= 0; else t0 <= i0[6:5];
  wire [30:0] t1 = t0;
  reg [30:0] t2;
  always @(*) begin
    t2 = ((i0 <= (t0 ? i1 : i0)) + (t0 << i3));
    casez ({i2[3], i1[27]})
      2'b1?: t2 = (-$unsigned((i0 * i1)));
      2'b01: t2 = ((i2 - (i2 >> i3)) & $unsigned(i2[21:21]));
      default: ;
    endcase
  end
  reg [30:0] t3;
  always @(posedge clock) begin
    if (reset) t3 <= 55097;
    else if (({t2, t2} && {t1, t0, i2})) t3 <= (((&(i3 ? 4 : i0)) - (~|t2)) << 2);
  end
  wire [1:0] t4 = ((t3 + t2[4]) ? t1[20:6] : {i3, i3, i0[5:1]});
  reg [64:0] t5;
  always @(*) begin
    t5 = t4[0:0];
    casez ({i0[3], t3[16]})
      2'b1?: t5 = 5;
      2'b01: t5 = $signed($unsigned(7));
      default: ;
    endcase
  end
  reg signed [31:0] l0;
  always @(*) if (!clock && (reset || (|t4[1:1]))) l0 = reset ? 0 : {1{i0}};
  localparam [31:0] P0 = 1;
  reg [3:0] fb;
  always @(posedge clock) if (reset) fb <= 0; else fb <= fb + i1;
  wire [4:0] sub_o;
  fz_sub u_sub(.clock(clock), .reset(reset), .a(P0), .b(t4), .y(sub_o));
  assign o0 = t0;
  assign o1 = l0;
  assign o2 = t2;
endmodule
module fz_sub(input clock, input reset, input [7:0] a, input [7:0] b, output [4:0] y);
  assign y = a + b;
endmodule
