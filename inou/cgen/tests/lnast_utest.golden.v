module lnast_utest (input a_i,
                    input b_i,
                    input c_i,
                    input d_i,
                    input e_i,
                    input f_i,
                    output o1_o,
                    output o2_o,
                    output s_o
                    output [2:0] s2_o);
  wire result_o1;
  wire result_o2;
  wire x;
  wire y;

  lnast_utest_fun1(.a_i(3), .b_i(4), .o1_o(result_o1), .o2_o(result_o2));

  always_comb begin
    x = 0
    y = 0
    *(LNAST: $a as __bits:1)*
    *(LNAST: $b as __bits:1)*
    *(LNAST: %s as __bits:1)*
    s_o = a_i & b_i;
    *(LNAST: %s2 as __bits:3)*
    s2_o = result_o1 + result_o2
    x = a_i;
    if (a_i > 1) {
      x = e_i;
      if (a_i > 2) {
        x = b_i;
      } elif ((a_i + 1) > 3) {
        x = c_i;
      } else {
        x = d_i;
      }
      y = e_i;
    } else {
      x = f_i;
    }
    o1_o = x + a_i;
    o2_o = y + a_i;
  end
end module
