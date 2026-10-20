// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#include <stdexcept>

#include "abc_backend.hpp"
#include "io_resynth.hpp"

namespace livehd::abc {
namespace {
synth::Io_mapper make_mapper(const std::map<std::string, std::string>& values) {
  Map_options options;
  options.library   = values.at("library");
  const auto string = [&](const char* key, std::string& target) {
    if (auto it = values.find(key); it != values.end()) {
      target = it->second;
    }
  };
  string("flow", options.flow);
  string("pre", options.pre);
  string("load", options.load);
  string("delay", options.delay);
  string("area_flow", options.area_flow);
  string("large_flow", options.large_flow);
  string("reg_margin", options.reg_margin);
  string("boundary_drive", options.boundary_drive);
  const auto integer = [&](const char* key, auto& target) {
    if (auto it = values.find(key); it != values.end()) {
      target = std::stoull(it->second);
    }
  };
  integer("max_fanout", options.max_fanout);
  integer("area_relax", options.area_relax_pct);
  integer("large_ge", options.large_ge);
  integer("time_budget_ms", options.time_budget_ms);
  integer("memory_budget_mb", options.memory_budget_mb);
  auto             shared_options = std::make_shared<Map_options>(options);
  auto             backend        = std::make_shared<Abc_backend>(options);
  synth::Io_mapper result;
  result.bindings     = [backend](hhds::Graph& graph) { return backend->io_region(graph, {}, {}, 1, 0, false, true).bindings; };
  result.attach_store = [backend, shared_options](std::shared_ptr<synth::Io_store> store) {
    shared_options->io_feedback = store;
    backend->set_io_feedback(std::move(store));
  };
  result.check  = [backend](hhds::Graph&                 graph,
                            const synth::Io_environment& env,
                            const synth::Io_environment& used,
                            double                       period,
                            double                       percent,
                            bool apply) { return backend->io_region(graph, env, used, period, percent, apply); };
  result.refine = [shared_options](hhds::GraphLibrary& library, const std::string& top) {
    auto options               = *shared_options;
    options.io_boundary_timing = true;
    options.reg_margin         = "0";  // recorded mapping budgets already include it
    Mapper                       driver(options);
    std::map<std::string, float> budgets;
    if (options.io_feedback) {
      for (const auto& [name, mapping] : options.io_feedback->mappings) {
        if (mapping.budget_ps > 0) {
          budgets[name] = mapping.budget_ps;
        }
      }
    }
    driver.set_region_delay_targets(std::move(budgets));
    driver.set_outlib(&library);
    const auto dff = liberty::resolve_dff_cells(options.library, options.dff_cell);
    driver.set_dff_cells(dff);
    driver.refine_boundaries(library, top);
    driver.stop();
  };
  return result;
}
const bool registered = synth::register_io_mapper("abc", make_mapper);
}  // namespace
}  // namespace livehd::abc
