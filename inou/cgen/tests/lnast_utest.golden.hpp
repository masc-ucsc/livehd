
#include "Lnast_utest_fun1.hpp"

class Lnast_utest {
private:
  uint32_t o1_o_next;
  uint32_t o2_o_next;
  uint32_t s_o_next;
  uint32_t s2_o_next;

public:
  uint32_t o1_o;
  uint32_t o2_o;
  uint32_t s_o;
  uint32_t s2_o;

  void combinational(uint32_t a_i, uint32_t b_i, uint32_t c_i, uint32_t d_i, uint32_t e_i, uint32_t f_i);
  void sequential();
}
