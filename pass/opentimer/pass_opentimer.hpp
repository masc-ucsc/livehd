//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "node_pin.hpp"
#include "ot/timer/timer.hpp"
#include "pass.hpp"
#include "power_vcd.hpp"

class Pass_opentimer : public Pass {
protected:
  absl::flat_hash_map<Node_pin::Compact_driver, std::string> overwrite_dpin2net;

  ot::Timer timer;

  int margin;  // % margin to mark nodes

  float freq;          // Frequency for power estimation
  std::string odir;    // output directory power trace
  float max_delay;     // slowest arrival time (delay) on the circuit
  float margin_delay;  // time delay to mark any slower cell for criticality

  std::vector<std::string> sdc_file_list;
  std::vector<std::string> spef_file_list;
  std::vector<std::string> vcd_file_list;
  std::vector<Power_vcd>   vcd_list;

  static void liberty_open(Eprp_var &var);
  static void time_work(Eprp_var &var);
  static void power_work(Eprp_var &var);

  void read_files();

  void set_input_delays(const std::string &pname);
  void set_output_delays(const std::string &pname);
  void build_circuit(Lgraph *lg);

  void read_vcd();
  void read_sdc_spef();
  void read_sdc(std::string_view sdc_file);
  void compute_timing(Lgraph *lg);
  void compute_power(Lgraph *lg);
  void populate_table(Lgraph *lg);

  std::string get_driver_net_name(const Node_pin &dpin) const;
  void        backpath_set_color(Node &node, int color);

public:
  Pass_opentimer(const Eprp_var &var);

  static void setup();
};
