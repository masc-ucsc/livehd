// Golden for std_clog2_widths.prp: `$clog2` of the same comptime values.
module top (
  input  [7:0]  a,
  input  [15:0] v,
  input  [3:0]  i,
  input  [2:0]  s,
  output [3:0]  y,
  output [7:0]  w,
  output        o,
  output [7:0]  iw,
  output [7:0]  z,
  output reg [7:0] r
);
  assign y  = a[3:0];
  assign w  = 8'd4;
  assign o  = v[i];
  assign iw = 8'd4;
  assign z  = 8'd22;
  always @(*) begin
    case (s)
      3'd1: r = 8'd2;
      3'd2: r = 8'd3;
      3'd3: r = 8'd4;
      3'd4: r = 8'd4;
      3'd5: r = 8'd4;
      3'd6: r = 8'd5;
      3'd7: r = 8'd5;
      default: r = 8'd0;
    endcase
  end
endmodule
