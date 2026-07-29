// Regression: cprop folding a Sum's constants to a NEGATIVE total.
//
// `0 - ua - 5` folds its constants to result = -5, and cprop then wired that
// negative value to the Sum's `bs` (subtract) sink -- which negates it a second
// time, so the node computed `-ua + 5`. The subtract sink must receive the
// MAGNITUDE.
module neg_const_fold_sum(input [2:0] ua, output [15:0] y);
  assign y = -(ua + 3'b101);
endmodule
