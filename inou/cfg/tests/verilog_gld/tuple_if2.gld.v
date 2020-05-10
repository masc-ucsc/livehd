module tuple_if2_gld (
  output [5:0] out1
  /* output [4:0] out2, */
  /* output [4:0] out3 */
);

wire [3:0] bar = 3'd7 == 3'd7 ? 4'd13 : 3'd4;
wire [4:0] foo = 3'd7 == 3'd7 ? 5'd17 : 3'd7;
assign out1 = bar + foo;

/* wire [4:0] x = 3'd7 == 3'd7 ? 5'd17 : 2'd2; */
/* assign out3 = x; */

endmodule
