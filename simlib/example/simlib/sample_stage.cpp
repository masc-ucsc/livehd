#include "livesim_types.hpp"
#include "sample_stage.hpp"

#ifdef SIMLIB_VCD
Sample_stage::Sample_stage(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter* writer)
    : hidx(_hidx)
    , scope_name(parent_name.empty() ? "sample" : parent_name + ".sample")
    , vcd_writer(writer)
    , s1(33, scope_name, writer)
    , s2(2123, scope_name, writer)
    , s3(122, scope_name, writer) {
  // this->vcd_writer=writer;
  // FIXME: populate random reset (random per variable)
}
void Sample_stage::vcd_reset_cycle() {
//  vcd_writer->change(vcd_reset, "1");  // as long as the reset is called, it would be 1
  vcd_writer->change(parent_vcd_reset, "1");
  s1.vcd_reset_cycle();
  s2.vcd_reset_cycle();
  s3.vcd_reset_cycle();
}

void Sample_stage::vcd_negedge() {
//  vcd_writer->change(vcd_clk, "0");
  vcd_writer->change(parent_vcd_clk, "0");
  s1.vcd_negedge();
  s2.vcd_negedge();
  s3.vcd_negedge();
}

void Sample_stage::vcd_posedge() {
//  vcd_writer->change(vcd_clk, "1");
  vcd_writer->change(parent_vcd_clk, "1");
//  vcd_writer->change(vcd_reset, "0");
  vcd_writer->change(parent_vcd_reset, "0");
  s1.vcd_posedge();
  s2.vcd_posedge();
  s3.vcd_posedge();
}

void Sample_stage::vcd_comb() {
  auto s1_to2_aValid = s1.to2_aValid;
  auto s1_to2_a      = s1.to2_a;
  auto s1_to2_b      = s1.to2_b;
  auto s1_to3_cValid = s1.to3_cValid;
  auto s1_to3_c      = s1.to3_c;
  s1.vcd_comb(s3.to1_b, s2.to1_aValid, s2.to1_a);
//  vcd_writer->change(vcd_to2_aValid, s1.to2_aValid.to_string_binary());
//  vcd_writer->change(vcd_to2_a, s1.to2_a.to_string_binary());
//  vcd_writer->change(vcd_to2_b, s1.to2_b.to_string_binary());
//  vcd_writer->change(vcd_to3_cValid, s1.to3_cValid.to_string_binary());
//  vcd_writer->change(vcd_to3_c, s1.to3_c.to_string_binary());
//  vcd_writer->change(vcd_to1_b, s3.to1_b.to_string_binary());
//  vcd_writer->change(vcd_to1_a, s2.to1_a.to_string_binary());
//  vcd_writer->change(vcd_to1_aValid, s2.to1_aValid.to_string_binary());

  auto s2_to3_dValid = s2.to3_dValid;
  auto s2_to3_d      = s2.to3_d;
  s2.vcd_comb(s1_to2_aValid, s1_to2_a, s1_to2_b);
//  vcd_writer->change(vcd_to3_dValid, s2.to3_dValid.to_string_binary());
//  vcd_writer->change(vcd_to3_d, s2.to3_d.to_string_binary());

  s3.vcd_comb(s1_to3_cValid, s1_to3_c, s2_to3_dValid, s2_to3_d);
}
#else
Sample_stage::Sample_stage(uint64_t _hidx) : hidx(_hidx), s1(33), s2(2123), s3(122) {
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
#endif
#ifdef SIMLIB_TRACE
void Sample_stage::add_signature(Simlib_signature &s) {
  s1.add_signature(s);
  s2.add_signature(s);
  s3.add_signature(s);
}
#endif
