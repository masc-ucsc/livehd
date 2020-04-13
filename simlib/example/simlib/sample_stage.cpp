#include "livesim_types.hpp"

#include "sample_stage.hpp"

Sample_stage::Sample_stage(uint64_t _hidx)
  : hidx(_hidx)
  , s1(33)
  , s2(2123)
  , s3(122) {
  // FIXME: populate random reset (random per variable)
}

void Sample_stage::reset_cycle() {

  s1.reset_cycle();
  s2.reset_cycle();
  s3.reset_cycle();
}

void Sample_stage::cycle() {

  auto s1_to2_aValid = s1.to2_aValid;
  auto s1_to2_a      = s1.to2_a;
  auto s1_to2_b      = s1.to2_b;
  auto s1_to3_cValid = s1.to3_cValid;
  auto s1_to3_c      = s1.to3_c;
  s1.cycle(s3.to1_b, s2.to1_aValid, s2.to1_a);

  auto s2_to3_dValid = s2.to3_dValid;
  auto s2_to3_d      = s2.to3_d;
  s2.cycle(s1_to2_aValid, s1_to2_a, s1_to2_b);

  s3.cycle(s1_to3_cValid, s1_to3_c, s2_to3_dValid, s2_to3_d);

}

void Sample_stage::add_signature(Simlib_signature &s) {
  s1.add_signature(s);
  s2.add_signature(s);
  s3.add_signature(s);
}
