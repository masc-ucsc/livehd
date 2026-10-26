// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "dreamplace/native/uroute_weights.hpp"

namespace livehd::placement {
struct Uroute_options {
  std::string dir             = "@pretrained";
  bool        export_features = true;
  int         shots = 5, min_shots = 1;
  std::string fallback  = "nearest";
  double      drc_level = 0;
};
struct Uroute_identity {
  std::string top, technology, macro_signature;
  double      die_width = 0, die_height = 0, utilization = 0;
  double      dbu_per_micron = 1;
};
struct Uroute_selection {
  std::shared_ptr<const DreamPlace::native::UrouteContext> context;
  std::string                                              skipped, cache_identity;
  std::string                                              manifest_sha256, weights_sha256;
  std::vector<std::string>                                 sample_ids;
};
// Read-only immutable manifest/weight snapshot. Unknown or unusable data is
// reported through select(). Invalid option ranges are configuration errors.
class Uroute_pool {
public:
  explicit Uroute_pool(Uroute_options);
  Uroute_selection   select(const DreamPlace::native::UrouteMaps&, const Uroute_identity&) const;
  const std::string& cache_identity() const;
  const std::string& skipped() const;

private:
  struct Data;
  std::shared_ptr<const Data> data_;
};
std::string uroute_digest(std::string_view);
// Include the exact generated proxy LEF bytes used by the routed handoff.
std::string uroute_technology(const std::vector<std::string>& lefs, std::string_view generated_lef = {});
void        validate_uroute_options(const Uroute_options&);
// Same native feature function as inference. The metadata commit is renamed
// last, so an offline reader can verify hashes before ingesting an export.
void export_uroute(const std::filesystem::path&, const DreamPlace::native::Design&, const Uroute_identity&, int bins_x, int bins_y,
                   double capacity_h, double capacity_v);
}  // namespace livehd::placement
