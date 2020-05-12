module tuple_if4_gld (
  output [4:0] out2
);


wire [4:0] tmp = 3'd7 == 3'd7 ? 5'd17 : 3'd7;
assign out2 = 3 + tmp;

endmodule
