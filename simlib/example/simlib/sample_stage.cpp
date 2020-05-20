#include "livesim_types.hpp"
#include "sample_stage.hpp"

/*#ifdef SIMLIB_VCD
Sample_stage::Sample_stage(uint64_t _hidx, vcd::VCDWriter &vcd_writer)//, std::string parent_name)
  : hidx(_hidx)
  , s1(33)//, concat(parent_name,".sample"))
  , s2(2123)
  , s3(122) {
  // FIXME: populate random reset (random per variable)
  }
#else*/
Sample_stage::Sample_stage(uint64_t _hidx)//, std::string parent_name)
  : hidx(_hidx)
  , s1(33)//, concat(parent_name,".sample"))
  , s2(2123)
  , s3(122) {
  // FIXME: populate random reset (random per variable)
   }
//#endif
void Sample_stage::reset_cycle() {

  s1.reset_cycle();
  s2.reset_cycle();
  s3.reset_cycle();
}

#ifdef SIMLIB_VCD
  void Sample_stage::vcd_cycle() {

    auto s1_to2_aValid = s1.to2_aValid;
    auto s1_to2_a      = s1.to2_a;
    auto s1_to2_b      = s1.to2_b;
    auto s1_to3_cValid = s1.to3_cValid;
    auto s1_to3_c      = s1.to3_c;
    s1.vcd_cycle(s3.to1_b, s2.to1_aValid, s2.to1_a);
    //vcd::VarPtr vcd_to2_a = vcd_writer.register_var("sample","to2_a",vcd::VariableType::wire, 32 );
    auto s2_to3_dValid = s2.to3_dValid;
    auto s2_to3_d      = s2.to3_d;
    s2.vcd_cycle(s1_to2_aValid, s1_to2_a, s1_to2_b);

    s3.vcd_cycle(s1_to3_cValid, s1_to3_c, s2_to3_dValid, s2_to3_d);
  }
#else
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
#endif
#ifdef SIMLIB_TRACE
  void Sample_stage::add_signature(Simlib_signature &s) {
    s1.add_signature(s);
    s2.add_signature(s);
    s3.add_signature(s);
  }
#endif
