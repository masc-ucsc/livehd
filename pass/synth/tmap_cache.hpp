// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <functional>
#include <memory>

#include "region_cache.hpp"

namespace livehd::synth {
// Transactional wrapper for provider-owned mapped-region reuse. Verify the
// complete snapshot before HHDS loads any body. Publish a new immutable
// generation before atomically replacing the current pointer. Old generations
// remain readable by other invocations; the user owns their workdir lifetime.
class Tmap_cache {
public:
  Tmap_cache(std::string directory, uint64_t salt, uint64_t max_bytes, std::function<bool(std::string_view)> admission = {});
  ~Tmap_cache();
  Region_cache* regions() const { return cache.get(); }
  bool          save();
  bool          invalid() const { return invalid_snapshot; }
  bool          refused() const { return resource_refused; }

private:
  std::filesystem::path                 root, pending;
  std::unique_ptr<Region_cache>         cache;
  uint64_t                              byte_limit;
  std::function<bool(std::string_view)> admission;
  bool                                  invalid_snapshot = false, resource_refused = false;
  bool                                  admit();
  std::filesystem::path                 make_generation();
  std::string                           inventory(const std::filesystem::path& directory);
};
}  // namespace livehd::synth
