module tuple_if3_gld (
  output [4:0] out3
);


wire [4:0] x = 3'd7 == 3'd7 ? 5'd17 : 2'd2;
assign out3 = x;

endmodule
