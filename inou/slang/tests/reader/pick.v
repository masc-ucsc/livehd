module pick(
  input [0:5] a_big
  ,input [5:0] a_little
  ,output [5:0] x_ll
  ,output [0:5] x_lb
  ,output [5:0] x_bl
  ,output [0:5] x_bb
);
  assign x_ll = a_little[4:0];
  assign x_lb = a_little[4:0];

  assign x_bb = a_big[0:4];
  assign x_bl = a_big[0:4];
endmodule

