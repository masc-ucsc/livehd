#include "livesim_types.hpp"
#include <bitset>
#include <stdio.h>
#include <chrono>
#include "sample3_stage.hpp"

#ifdef SIMLIB_VCD

Sample3_stage::Sample3_stage(uint64_t _hidx, std::string &parent_name)
  : hidx(_hidx)
  , scope_name(parent_name+".s3") {
  }

void Sample3_stage::vcd_reset_cycle() {
  tmp  = 0;
  tmp2 = 0;

	reset_iterator = reset_iterator + 1;
  vcd_writer.change(vcd_reset_iterator, t ,'b'+ std::bitset<8>(reset_iterator).to_string());
	memory[reset_iterator] = 0;
}

void Sample3_stage::vcd_cycle(UInt<1> s1_to3_cValid, UInt<32> s1_to3_c, UInt<1> s2_to3_dValid, UInt<32> s2_to3_d) {
  if (__builtin_expect(((tmp & UInt<32>(0xFFFF)) == UInt<32>(45339)),0)) {
    if ((tmp2 & UInt<32>(15)) == UInt<32>(0)) {
      printf("memory[127] = %ud\n",memory[127]);
    }
    tmp2 = tmp2.addw(UInt<32>(1));
  }
  
    to1_b = memory[(tmp&UInt<32>(0xff)).as_single_word()];
    vcd_writer.change(vcd_to1_b, t , to1_b.to_string_binary());
  
    if (s1_to3_cValid && s2_to3_dValid) {
      UInt<32> tmp3 = s1_to3_c.addw(tmp);
      memory[(tmp3 & UInt<32>(0xff)).as_single_word()] = s2_to3_d;
    }
  
    tmp = tmp.addw(UInt<32>(7));
  }
#else
Sample3_stage::Sample3_stage(uint64_t _hidx)
  : hidx(_hidx) {
  }

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

#endif

#ifdef SIMLIB_TRACE
void Sample3_stage::add_signature(Simlib_signature &s) {
  s.append(1102); // tmp
  s.append(333); // tmp2
  s.append(222); // ...
}

#endif
