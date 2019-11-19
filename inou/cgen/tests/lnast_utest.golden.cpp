
void Lnast_utest::combinational() {
  // LNAST: a_i as __bits:1
  // LNAST: b_i as __bits:1
  // LNAST: s_o as __bits:1
  s_o = a_i & b_i;
  result = lnast_utest_fun1.stateful(clk, reset, 3, 4);
  x = a_i;
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
  o1_o = x + a_i;
  o2_o = y + a_i;
}

void Lnast_utest::sequential() {
}

