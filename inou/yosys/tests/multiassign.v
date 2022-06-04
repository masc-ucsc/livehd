
module multiassign(output [4:0] out);

  assign { out[4], out[2:1] } = { 2'b11, out[3] }; //Also, isn't this tuple assignment inverted!?
  assign { out[3], out[0] } = 2'b01;

endmodule

