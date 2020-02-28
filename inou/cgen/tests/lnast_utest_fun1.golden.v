module lnast_utest_fun1 (input [1:0] a_i,
                         input [1:0] b_i,
                         output [1:0] o1_o,
                         output [1:0] o2_o);
  always_comb begin
    *(LNAST: $a as __bits:2)*
    *(LNAST: $b as __bits:2)*
    *(LNAST: $o1 as __bits:2)*
    *(LNAST: $o2 as __bits:2)*
    o1_o = a_i ^ b_i;
    o2_o = a_i & b_i;
  end
endmodule

