module lnast_utest_fun1 (input clk,
                         input reset,
                         input a_i,
                         input b_i,
                       output o_o);
  always @(*) begin
    o_o = a_i ^ b_i;
  end
endmodule

