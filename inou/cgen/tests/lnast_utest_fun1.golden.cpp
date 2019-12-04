
#include "Lnast_utest_fun1.hpp"

void Lnast_utest_fun1::combinational(uint32_t a_i, uint32_t b_i) {
  o_o_next = a_i ^ b_i;
}

void Lnast_utest_fun1::sequential() {
  o_o = o_o_next;
}

