module simple_phi (
//  output reg y, z, w,
  input a, b, c, d, sel
//  clk
);

reg t1, t2, t3, t4;
always @ (*) begin
  if (sel) begin
    t1 = a & b & c;
    t2 = a | b;
    t3 = t2 & c; // t3 == t1
  end
  else begin
    t1 = 0;
    t2 = c;
    t3 = 0;
  end
  t4 = a | b; // 
end

endmodule
/*
idx1 = a
idx2 = b
idx19 = ~idx1 // ~a
idx4 = idx19 & idx2 // ~a & b ===== t5
idx3 = idx4
idx6 = idx1 ^ idx2 // a ^ b ===== t4
idx5 = idx6
idx16 = idx6
idx17 = idx4

idx18 = ~idx2 // ~b
idx10 = idx1 & idx18 // a & ~b ===== t2
idx14 = idx10
idx9 = idx10
idx20 = ~idx1 // ~a
idx12 = idx20 & idx2 // ~a & b ===== t1
idx11 = idx12
idx13 = idx12
idx8 = idx12 | idx10 // ~a & b | a & ~b =====t3
idx7 = idx8
idx15 = idx8
*/
