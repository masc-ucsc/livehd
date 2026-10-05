// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "design_synth.hpp"
#include "mapped_cost.hpp"
#include "tmap.hpp"

namespace livehd::usyn {
struct Mapping_trial {
  bool                              p1 = false, multi_rep = false, selected = false;
  uint64_t                          work_limit = 0, work_used = 0;
  double                            elapsed_ms = 0;
  std::optional<synth::Mapped_cost> cost;
  std::string                       reason;
  uint64_t                          search_used = 0;
};
// Decision report, not a serialized cache artifact or physical-area prediction.
std::string native_report(const Logical_design& design, const Design_options& options, std::string_view tmap, double elapsed_ms,
                          uint64_t peak_bytes, std::string_view provenance_directory, std::string_view provenance,
                          std::span<const std::string> artifact_paths = {}, std::span<const Mapping_trial> trials = {});
// Region `delay` is emitted only when the provider reports picoseconds; an
// untimed map reports `logic_depth` levels and `"delay": null`.
std::string mapping_report(const synth::Mapped_design& design, std::string_view provider, const synth::Tmap_options& options);
}  // namespace livehd::usyn
