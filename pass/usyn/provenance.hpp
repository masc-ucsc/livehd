// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace livehd::usyn {
struct Provenance_limits {
  uint64_t bytes   = 64 * 1024 * 1024;
  uint32_t entries = 4096;
};
// Capture kernel-observed inputs immediately before mapping. Does not claim a
// complete frontend dependency closure or an atomic snapshot during compilation.
// Destination must be fresh; publication/rollback belongs to the caller.
// Read/refusal errors are recorded as incomplete; archive write errors throw.
std::string archive_provenance(const std::filesystem::path& destination, std::string_view context, std::string_view mapper_revision,
                               Provenance_limits limits = {});
}  // namespace livehd::usyn
