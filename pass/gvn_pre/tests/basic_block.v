module basic_block (
//  output reg y, z, w,
  input a, b
//  clk
);

reg t1, t2, t3, t4, t5;
always @ (*) begin
  t1 = ~a & b;
  t2 = a & ~b;
  t3 = t1 | t2;
  t4 = a ^ b;
  t5 = b & ~a;
//  y = t3;
//  z = t4;
//  w = t5;
end

endmodule
/*
idx1 = a: essential
idx2 = b: essential

idx19 = ~idx1 // ~a: essential

idx4 = idx19 & idx2 // b & ~a ===== t5: essential

idx3 = idx4
idx6 = idx1 ^ idx2 // a ^ b ===== t4: essential

idx5 = idx6
idx16 = idx6
idx17 = idx4

idx18 = ~idx2 // ~b: essential

idx10 = idx1 & idx18 // a & ~b ===== t2: essential

idx14 = idx10
idx9 = idx10
idx20 = ~idx1 // ~a: can be replaced by idx19
idx12 = idx20 & idx2 // ~a & b ===== t1: can be replaced by idx4
idx11 = idx12
idx13 = idx12
idx8 = idx12 | idx10 // ~a & b | a & ~b =====t3: essential

idx7 = idx8
idx15 = idx8
*/
