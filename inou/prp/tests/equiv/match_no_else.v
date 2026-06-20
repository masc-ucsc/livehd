module \match_no_else.msel (
  input  [1:0] x,
  input  [7:0] a,
  input  [7:0] b,
  input  [7:0] c,
  input  [7:0] d,
  output reg [7:0] res
);

  always @(*) begin
    case (x)
      2'd0:    res = a;
      2'd1:    res = b;
      2'd2:    res = c;
      default: res = d;
    endcase
  end

endmodule
