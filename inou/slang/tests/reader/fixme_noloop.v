module noloop( input [1:0] a, input [1:0] b, output [5:0] c);
  assign c[1:0] = a+b;
  assign c[3:2] = c[1:0]+1;
  assign c[5:4] = {c[3]^c[2],c[1]};
endmodule

