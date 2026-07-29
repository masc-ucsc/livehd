// Regression: an UNSIGNED module output declared `signed` by cgen.
//
// cgen used to spell every port `signed`. For inputs that is the contract
// upass.tolg lowers against (it wraps each unsigned input's body read in a
// to_positive Get_mask), but outputs have no such compensation -- and the lie
// is not harmless for an `output reg` that is also READ inside the module. A
// flop whose Q drives an output reuses the port as its storage, so the terminal
// -count compare `cv == 8'hff` came out as `cv == 9'shff` with `cv` declared
// `signed [7:0]`: -1 == 255, never true, so `tc` was stuck at 0. Silent
// miscompile -- it compiled clean and LEC refuted it.
module unsigned_output_reg_compare(input clk, output tc, output reg [7:0] cv);
  assign tc = (cv == 8'hff);
  always @(posedge clk) cv <= cv + 8'b1;
endmodule
