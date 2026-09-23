// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <map>

#include "tmap.hpp"

namespace livehd::synth {

// Exact backend/version + Liberty CONTENT context. An unreadable or oversized
// library disables this optional cache; mapping remains available.
std::string template_context(const std::filesystem::path& library, std::string_view backend_version);

// Reuses proved cell templates, never signal instances. The caller supplies an
// immutable backend/library context for one invocation, and still proves/times
// each stitched region and the final design. No ABC pointers are retained.
class Template_cache final : public Tmap_backend {
public:
  struct Statistics {
    uint64_t hits = 0, misses = 0, loaded = 0, evictions = 0;
  };
  Template_cache(Tmap_backend& backend, std::string context, size_t max_bytes = 16 * 1024 * 1024);
  Mapped_fragment   map(const Mapping_request& request) override;
  // Blocks are region-specific: never cached, always forwarded.
  Mapped_block      map_block(const Block_request& request) override { return backend_.map_block(request); }
  std::vector<std::pair<uint64_t, uint32_t>> lut_reference(const Logic_network& logic, uint32_t k, bool choices) override {
    return backend_.lut_reference(logic, k, choices);
  }
  // Loading is bounded and all-or-nothing. Missing, stale or corrupt records
  // are cold misses. Save ONLY to staging; the driver owns atomic publication.
  bool              load(const std::filesystem::path& file);
  bool              save(const std::filesystem::path& file) const;
  bool              enabled() const { return !context_.empty() && max_bytes_ != 0; }
  const Statistics& statistics() const { return statistics_; }
  size_t            entries() const { return entries_.size(); }
  size_t            bytes() const { return bytes_; }

private:
  Tmap_backend&                      backend_;
  std::string                        context_;
  size_t                             max_bytes_, bytes_ = 0;
  Statistics                         statistics_;
  // Exact keys rather than digest equality. Encoded fragments are decoded on
  // every hit, so caller mutations cannot alter the cached template.
  std::map<std::string, std::string> entries_;
};
}  // namespace livehd::synth
