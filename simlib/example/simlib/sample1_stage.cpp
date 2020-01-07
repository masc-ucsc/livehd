#include "livesim_types.hpp"

#include "sample1_stage.hpp"

void Sample1_stage::reset_cycle() {
  tmp = UInt<32>(0);
  to2_aValid = UInt<1>(0);
  to3_cValid = UInt<1>(0);
  to3_c = UInt<32>(0);
}

void Sample1_stage::cycle(UInt<32> s3_to1_b, UInt<1> s2_to1_aValid, UInt<32> s2_to1_a) {
  to2_b = s3_to1_b.addw(UInt<32>(1));

  auto tmp3 = s2_to1_a.addw(s3_to1_b);
  to2_a = tmp3.addw(UInt<32>(2));
  to2_aValid = s2_to1_aValid;

  to3_cValid =  tmp.bit<0>();
  to3_c = tmp.addw(s2_to1_a);

  tmp = tmp.addw(UInt<32>(23));
}

