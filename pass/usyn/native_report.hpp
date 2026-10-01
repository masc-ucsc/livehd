// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include "design_synth.hpp"
#include "tmap.hpp"

namespace livehd::usyn {
// Decision report, not a serialized cache artifact or physical-area prediction.
std::string native_report(const Logical_design& design, const Design_options& options, std::string_view tmap, double elapsed_ms,
                          uint64_t peak_bytes, std::string_view provenance_directory, std::string_view provenance,
                          std::span<const std::string> artifact_paths = {});
std::string mapping_report(const synth::Mapped_design& design, std::string_view provider, std::string_view library);
}  // namespace livehd::usyn
