// Golden for uncertain_whole_tuple_write.prp: a 2:1 mux on `c`.
module uncertain_whole_tuple_write (
  input        c,
  output [7:0] o
);
  assign o = c ? 8'd5 : 8'd1;
endmodule
