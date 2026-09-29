// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <string>
#include <string_view>

namespace livehd::usyn {
// A region's decision row (its JSON report) stored with its region-cache row,
// so an incremental hit still reports how the region was mapped. Evidence is a
// JSON object {"schema_version":2,"decision":<row>}; the row names its region.
std::string pack_evidence(std::string_view decision);
bool        valid_evidence(std::string_view evidence, std::string_view expected_region = {});
// The report of a reused region: its cached decision, labeled historical.
std::string replay_evidence(std::string_view region, std::string_view cached_region, std::string_view evidence);
}  // namespace livehd::usyn
