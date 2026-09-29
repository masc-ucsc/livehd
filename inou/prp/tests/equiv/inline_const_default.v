module inline_const_default(input [4:0] rs, output [2:0] o);
  assign o = (rs != 5'd0) ? 3'd2 : 3'd0;
endmodule
