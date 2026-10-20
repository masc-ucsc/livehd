// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "hhds/graph.hpp"

namespace livehd::synth {
class Region_cache;
struct Wire_load {
  double                       capacitance_ff_um = 0, resistance_ohm_um = 0;
  std::map<unsigned, double>   length_um;
  std::map<unsigned, unsigned> samples;
  bool                         empty() const { return length_um.empty() || capacitance_ff_um <= 0; }
};
struct Io_environment {
  std::map<std::string, double> inputs, required;
  Wire_load                     wire;
};
// A physical pin in the region's definition; bus port bits use name[bit].
struct Io_binding {
  std::string port, instance, pin;
  bool        input = false;
};
struct Io_decision {
  std::string             module, decision, input, output;
  bool                    checked = false, fired = false, accepted = false;
  double                  slack_before_ps = 0, slack_after_ps = 0, delta_ps = 0, seconds = 0;
  double                  no_wire_delay_ps = 0, wire_delay_ps = 0;
  double                  area  = 0;
  unsigned                gates = 0;
  std::vector<Io_binding> bindings;
};
// One private backend session per flow, shared by stage-level checks only.
struct Io_mapper {
  using Check = std::function<Io_decision(hhds::Graph&, const Io_environment&, const Io_environment&, double, double, bool)>;
  Check                                                        check;
  std::function<std::vector<Io_binding>(hhds::Graph&)>         bindings;
  std::function<void(std::shared_ptr<class Io_store>)>         attach_store;
  std::function<void(hhds::GraphLibrary&, const std::string&)> refine;
  Io_decision operator()(hhds::Graph& graph, const Io_environment& env, const Io_environment& used, double period, double percent,
                         bool apply) const {
    return check(graph, env, used, period, percent, apply);
  }
};
using Io_mapper_factory = std::function<Io_mapper(const std::map<std::string, std::string>&)>;
bool      register_io_mapper(std::string name, Io_mapper_factory factory);
Io_mapper make_io_mapper(const std::string& name, const std::map<std::string, std::string>& options);

// Persistent state is published once per completed placement stage, never in
// the GP timing callback. A cache miss invalidates timing, but not wire loads.
class Io_store {
public:
  struct Mapping {
    std::string flow;
    double      budget_ps = 0;
    bool        custom    = false;
  };
  std::map<std::string, Mapping> mappings;
  struct Row {
    std::string    netlist, stage, run;
    Io_environment used, placed;
  };
  explicit Io_store(std::string directory, double period_ps, bool persistent = true, std::string timing_identity = {});
  void                       invalidate(const std::string& module);
  void                       mapping(const std::string& module, Mapping value);
  bool                       record(const std::string& module, std::string identity, std::string stage, Io_environment environment);
  void                       used(const std::string& module, std::string identity, const Io_environment& environment);
  void                       save() const;
  const Wire_load&           wire(const std::string& module) const;
  std::map<std::string, Row> rows;
  std::map<std::string, Wire_load> wires;
  Wire_load                        global_wire;
  std::shared_ptr<Region_cache>    cache;
  std::string                      directory, run_id;
  double                           period_ps;

private:
  std::mutex  mapping_mutex_;
  bool        persistent_;
  std::string timing_identity_;
};
std::string mapped_identity(hhds::Graph& graph);
std::string environment_identity(const Io_environment& environment);
std::string describe_environment(const Io_environment& environment);
int         io_stage_rank(const std::string& stage);
bool        io_trigger(double slack_ps, double tightening_ps, double period_ps, double percent);
}  // namespace livehd::synth
