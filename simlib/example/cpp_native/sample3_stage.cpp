
#include <stdio.h>

#include "sample3_stage.hpp"

void Sample3_stage::reset_cycle() {
  tmp  = 0;
  tmp2 = 0;

	reset_iterator = reset_iterator + 1;
	memory[reset_iterator] = 0;
}

void Sample3_stage::cycle(bool s1_to3_cValid, uint32_t s1_to3_c, bool s2_to3_dValid, uint32_t s2_to3_d) {
  if (__builtin_expect(((tmp & 0xFFFF) == 45339),0)) {
    if ((tmp2 &15) == 0) {
      printf("memory[127] = %ud\n",memory[127]);
    }
    tmp2 = tmp2 + 1;
  }

  to1_b = memory[tmp&0xff];

  if (s1_to3_cValid && s2_to3_dValid) {
    memory[(s1_to3_c + tmp) & 0xff] = s2_to3_d;
  }

  tmp = tmp + 7; // A prime number
}
