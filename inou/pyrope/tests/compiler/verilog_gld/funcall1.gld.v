
module funcall1(out);
  output [7:0] out; // more bits than needed. Still OK
  assign out = 34;
endmodule
