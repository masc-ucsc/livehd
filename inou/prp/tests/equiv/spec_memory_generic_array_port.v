// Golden for spec_memory_generic_array_port: arrays are packed buses (entry k
// at bits [k*W +: W]).
module spec_memory_generic_array_port(input [31:0] v4, input [1:0] ix4, input [9:0] v2, input ix2,
                                      output [7:0] y8, output [4:0] y5,
                                      output [31:0] o8, output [9:0] o5);
  assign y8 = v4[ix4*8 +: 8];
  assign y5 = v2[ix2*5 +: 5];
  assign o8 = {v4[7:0], v4[15:8], v4[23:16], v4[31:24]};
  assign o5 = {v2[4:0], v2[9:5]};
endmodule
