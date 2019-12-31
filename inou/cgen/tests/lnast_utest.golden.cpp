
#include "lnast_utest.golden.hpp"

void Lnast_utest::combinational(uint32_t a_i, uint32_t b_i, uint32_t c_i, uint32_t d_i, uint32_t e_i, uint32_t f_i) {
  uint32_t result = 0;
  uint32_t x      = 0;
  uint32_t y      = 0;

  // LNAST: a_i as __bits:1
  // LNAST: b_i as __bits:1
  // LNAST: s_o as __bits:1
  s_o_next = a_i & b_i;
  result   = lnast_utest_fun1.stateful(3, 4);
  x        = a_i;
  if (a_i > 1) {
    x = e_i;
    if (a_i > 2) {
      x = b_i;
    } else if (a_i + 1 > 3) {
      x = c_i;
    } else {
      x = d_i;
    }
    y = e_i;
  } else {
    x = f_i;
  }
  o1_o_next = x + a_i;
  o2_o_next = y + a_i;
}

void Lnast_utest::sequential() {
  o1_o = o1_o_next;
  o2_o = o2_o_next;
  s_o  = s_o_next;
}
