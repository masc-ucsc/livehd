
#include "Lnast_utest_fun1"

class Lnast_utest {
private:
  uint32_t clk;
  bool reset;
  uint32_t a_i;
  uint32_t b_i;
  uint32_t c_i;
  uint32_t d_i;
  uint32_t e_i;
  uint32_t f_i;

  uint32_t result;
  uint32_t x;
  uint32_t y;
public:
  uint32_t o1_o;
  uint32_t o2_o;
  uint32_t s_o;

  void combinational();
  void sequential();
}

