module uif_select(
  input  [1:0] sel,
  input  [7:0] base,
  input  [7:0] a,
  input  [7:0] b,
  output [7:0] y
);
assign y = (sel == 2'd0) ? a :
           (sel == 2'd1) ? b :
                            base;
endmodule
