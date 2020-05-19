#include "livesim_types.hpp"

#include "sample1_stage.hpp"

Sample1_stage::Sample1_stage(uint64_t _hidx)//, const std::string &parent_name)
  : hidx(_hidx) {
}

void Sample1_stage::reset_cycle() {
  tmp = UInt<32>(0);
  to2_aValid = UInt<1>(0);
  to3_cValid = UInt<1>(0);
  to3_c = UInt<32>(0);
}
#ifdef SIMLIB_VCD
  void Sample1_stage::vcd_cycle(UInt<32> s3_to1_b, UInt<1> s2_to1_aValid, UInt<32> s2_to1_a) {
    to2_b = s3_to1_b.addw(UInt<32>(1));
  
    auto tmp3 = s2_to1_a.addw(s3_to1_b);
    to2_a = tmp3.addw(UInt<32>(2));
    to2_aValid = s2_to1_aValid;
  
    to3_cValid =  tmp.bit<0>();
    to3_c = tmp.addw(s2_to1_a);
  
    tmp = tmp.addw(UInt<32>(23));
  }
#else
  void Sample1_stage::cycle(UInt<32> s3_to1_b, UInt<1> s2_to1_aValid, UInt<32> s2_to1_a) {
    to2_b = s3_to1_b.addw(UInt<32>(1));
  
    auto tmp3 = s2_to1_a.addw(s3_to1_b);
    to2_a = tmp3.addw(UInt<32>(2));
    to2_aValid = s2_to1_aValid;
  
    to3_cValid =  tmp.bit<0>();
    to3_c = tmp.addw(s2_to1_a);
  
    tmp = tmp.addw(UInt<32>(23));
  }
#endif
#ifdef SIMLIB_TRACE
void Sample1_stage::add_signature(Simlib_signature &s) {
  s.append(hidx);
  s.append(33 );  // 33 is the semantic ID (hash inputs + op + bits) for to2_aValid
  s.append(103);
  s.append(203);
  s.append(11 );
  s.append(33 );
  s.append(2  );

  s.append(202); // memory signature (ports/size/...) semantic ID (sid)
}
#endif

