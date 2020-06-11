
#include "livesim_types.hpp"
#include "sample2_stage.hpp"

#ifdef SIMLIB_VCD
Sample2_stage::Sample2_stage(uint64_t _hidx, const std::string &parent_name, vcd::VCDWriter *writer)
    : hidx(_hidx), scope_name(parent_name + ".s2"), vcd_writer(writer) {}

void Sample2_stage::vcd_posedge() {
  vcd_writer->change(vcd_clk, "1");
  vcd_writer->change(vcd_reset, "0");
}
void Sample2_stage::vcd_negedge() { vcd_writer->change(vcd_clk, "0"); }

void Sample2_stage::vcd_reset_cycle() {
  vcd_writer->change(vcd_reset, "1");
  tmp        = 1;
  vcd_writer->change(vcd_tmp, "1");
  to3_dValid = false;
  vcd_writer->change(vcd_to3_dValid, to3_dValid.to_string_binary());
  to2_eValid = false;
  vcd_writer->change(vcd_to2_eValid, to2_eValid.to_string_binary());
  to1_aValid = false;
  vcd_writer->change(vcd_to1_aValid, to1_aValid.to_string_binary());
}
#else
Sample2_stage::Sample2_stage(uint64_t _hidx) : hidx(_hidx) {}

void Sample2_stage::reset_cycle() {
  tmp        = 1;
  to3_dValid = false;
  to2_eValid = false;
  to1_aValid = false;
}
#endif
#ifdef SIMLIB_TRACE
void Sample2_stage::add_signature(Simlib_signature &s) {
  s.append(11002);  // tmp
  s.append(33);     // to3_dValid
  s.append(2222);   //...
  s.append(222);    //...
}
#endif
