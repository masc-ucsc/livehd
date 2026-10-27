// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <utility>

#include "pass/synth/io_resynth.hpp"
#include "place.hpp"

namespace livehd::placement {
// Stage-level orchestration; no callback from individual GP iterations.
class Io_timing_flow {
public:
  Io_timing_flow(std::shared_ptr<hhds::Graph> source, const Options& options, std::shared_ptr<synth::Io_store> store,
                 const std::map<std::string, std::string>& mapper_options, Evaluate evaluate, double percent);
  bool before_placement();
  // prior must describe exactly placed under the same constraints/model.
  // Approximate rankings never replace the detailed resynthesis measurement.
  bool after_stage(Placement_stage stage, const Result& placed, std::optional<Timing_feedback> prior = std::nullopt);
  // One-use handoff from an unchanged completed final stage to physical repair.
  std::optional<Timing_feedback> take_final_timing() { return std::exchange(final_timing_, std::nullopt); }
  std::string                    report() const;

private:
  bool check(const std::string& stage, const std::map<std::string, synth::Io_environment>* measurements, bool automatic);
  std::map<std::string, synth::Io_environment> harvest(const Result& placed, const Timing_feedback& timing);
  std::shared_ptr<hhds::Graph>                 source_;
  Options                                      options_;
  std::shared_ptr<synth::Io_store>             store_;
  synth::Io_mapper                             mapper_;
  Evaluate                                     evaluate_;
  std::optional<Timing_feedback>               final_timing_;
  double                                       percent_;
  bool                                         first_stage_ = true, automatic_used_ = false;
  unsigned                                     restarted_ = 0, setup_met_stages_ = 0;
  std::string                                  restarted_stage_;
  struct Sample {
    std::string           stage;
    synth::Io_decision    decision;
    synth::Io_environment placed, used;
    bool                  automatic = false;
  };
  std::vector<Sample> samples_;
};
}  // namespace livehd::placement
