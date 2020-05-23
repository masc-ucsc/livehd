#pragma once
#include <string>
#include "sample1_stage.hpp"
#include "sample2_stage.hpp"
#include "sample3_stage.hpp"
#include "vcd_writer.hpp"

struct Sample_stage {
  uint64_t hidx;
  #ifdef SIMLIB_VCD
    std::string scope_name;
    vcd::VCDWriter* vcd_writer;

  Sample_stage(uint64_t _hidx, std::string &parent_name, vcd::VCDWriter* writer);
  void vcd_reset_cycle();
  void vcd_cycle();
   // vcd::VCDWriter &vcd_writer = vcd::initialize_vcd_writer();
    vcd::VarPtr vcd_to2_aValid = vcd_writer->register_var(scope_name,"to2_aValid",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_to2_a = vcd_writer->register_var(scope_name,"to2_a[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to2_b = vcd_writer->register_var(scope_name,"to2_b[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to3_cValid = vcd_writer->register_var(scope_name,"to3_cValid",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_to3_c = vcd_writer->register_var(scope_name,"to3_c[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to3_dValid = vcd_writer->register_var(scope_name,"to3_dValid",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_to3_d = vcd_writer->register_var(scope_name,"to3_d[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to1_aValid = vcd_writer->register_var(scope_name,"to1_aValid",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_to1_a = vcd_writer->register_var(scope_name,"to1_a[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to2_eValid = vcd_writer->register_var(scope_name,"to2_eValid",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_to2_e = vcd_writer->register_var(scope_name,"to2_e[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_to1_b = vcd_writer->register_var(scope_name,"to1_b[31:0]",vcd::VariableType::wire, 32 );
    vcd::VarPtr vcd_clk = vcd_writer->register_var(scope_name,"clk",vcd::VariableType::wire, 1 );
    vcd::VarPtr vcd_reset = vcd_writer->register_var(scope_name,"reset",vcd::VariableType::wire, 1 );
#else
  Sample_stage(uint64_t _hidx);
  void reset_cycle();
  void cycle();
#endif
  Sample1_stage s1;
  Sample2_stage s2;
  Sample3_stage s3;

#ifdef SIMLIB_TRACE
  void add_signature(Simlib_signature &sign);
#endif
};
