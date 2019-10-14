module lnast_utest (input clk,
                    input reset,
                    input [0] a_i,
                    input [0] b_i,
                    input     c_i,
                    input     d_i,
                    input     e_i,
                    input     f_i,
                    output     o1_o,
                    output     o2_o,
                    output [0] s_o);
  wire result;
  wire x;
  wire y;

  always @(*) begin
    *(LNAST: a_i as __bits:1)*
    *(LNAST: b_i as __bits:1)*
    *(LNAST: s_o as __bits:1)*
    s_o = a_i & b_i;
    result = lnast_utest_fun1(clk, reset, a:3, b:4);
    x = a_i;
    if (a_i > 1) {
      x = e_i;
      if (a_i > 2) {
        x = b_i;
      } elif (a_i + 1 > 3) {
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
