#include "livesim_types.hpp"

#include <stdio.h>

#include "sample3_stage.hpp"

void Sample3_stage::reset_cycle() {
  tmp  = 0;
  tmp2 = 0;

	reset_iterator = reset_iterator + 1;
	memory[reset_iterator] = 0;
}

void Sample3_stage::cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d) {
  if (__builtin_expect(((tmp & UInt<32>(0xFFFF)) == UInt<32>(45339)),0)) {
    if ((tmp2 & UInt<32>(15)) == UInt<32>(0)) {
      printf("memory[127] = %ud\n",memory[127]);
    }
    tmp2 = tmp2.addw(UInt<32>(1));
  }

  to1_b = memory[(tmp&UInt<32>(0xff)).as_single_word()];

  if (s1_to3_cValid && s2_to3_dValid) {
    UInt<32> tmp3 = s1_to3_c.addw(tmp);
    memory[(tmp3 & UInt<32>(0xff)).as_single_word()] = s2_to3_d;
  }

  tmp = tmp.addw(UInt<32>(7));
}
