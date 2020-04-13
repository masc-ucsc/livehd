#include "livesim_types.hpp"

#include "sample2_stage.hpp"

Sample2_stage::Sample2_stage(uint64_t _hidx)
  : hidx(_hidx) {
}

void Sample2_stage::reset_cycle() {
  tmp = 1;
  to3_dValid = false;
  to2_eValid = false;
  to1_aValid = false;
}

void Sample2_stage::add_signature(Simlib_signature &s) {
  s.append(11002); // tmp
  s.append(33); // to3_dValid
  s.append(2222); //...
  s.append(222); //...
}
