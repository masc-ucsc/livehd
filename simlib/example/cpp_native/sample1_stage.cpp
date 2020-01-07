
#include "sample1_stage.hpp"

void Sample1_stage::reset_cycle()
{
  tmp = 0;
  to2_aValid = false;
  to3_cValid = false;
  to3_c = 0;
}


void Sample1_stage::cycle(uint32_t s3_to1_b, bool s2_to1_aValid, uint32_t s2_to1_a) {
  to2_b = s3_to1_b + 1;

  to2_a      = s2_to1_a + s3_to1_b + 2;
  to2_aValid = s2_to1_aValid;

  to3_cValid =  (tmp & 1) != 0;
  to3_c = tmp + s2_to1_a;

  tmp = tmp + 23;
}
