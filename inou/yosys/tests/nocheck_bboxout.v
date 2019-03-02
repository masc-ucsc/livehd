
module nocheck_bboxout(input [3:0] top_in, output [1:0] top_out);

  not_used i0 (.bb_in(top_in), .bb_out(top_out));

endmodule

