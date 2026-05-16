//  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <memory>
#include <string>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "hhds/graph.hpp"
#include "ot/timer/timer.hpp"
#include "pass.hpp"
#include "vcd_power.hpp"

class Pass_opentimer : public Pass {
protected:
  // Driver pin (hhds::Class_index) -> opentimer net name. Used when a driver
  // pin has been renamed to a primary input/output net or to a slice of a bus.
  absl::flat_hash_map<hhds::Class_index, std::string> overwrite_dpin2net;

  ot::Timer timer;

  int    margin;  // % margin to mark nodes
  double freq;

  std::string odir;          // output directory power trace
  float       max_delay;     // slowest arrival time (delay) on the circuit
  float       margin_delay;  // time delay to mark any slower cell for criticality

  std::vector<std::string> sdc_file_list;
  std::vector<std::string> spef_file_list;
  std::vector<std::string> vcd_file_list;
  std::vector<Vcd_power>   vcd_list;

  static void liberty_open(Eprp_var& var);
  static void time_work(Eprp_var& var);
  static void power_work(Eprp_var& var);

  void read_files();

  void set_input_delays(const std::string& pname);
  void set_output_delays(const std::string& pname);
  void build_circuit(const std::shared_ptr<hhds::Graph>& g);

  void read_vcd();
  void read_sdc_spef();
  void read_sdc(std::string_view sdc_file);
  void compute_timing(const std::shared_ptr<hhds::Graph>& g);
  void compute_power(const std::shared_ptr<hhds::Graph>& g);
  void populate_table(const std::shared_ptr<hhds::Graph>& g);

  std::string get_driver_net_name(const hhds::Pin_class& dpin) const;
  void        backpath_set_color(hhds::Node_class node, int color);

public:
  Pass_opentimer(const Eprp_var& var);

  static void setup();
};
