// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "centroid_record.hpp"
#include "core/metadata.hpp"
#include "dreamplace/native/placement.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"
namespace livehd::placement {
// Logical scopes are captured before flattening/releasing the source library.
struct Hint_scope {
  std::string              module;
  std::vector<std::string> path;
  // Present on a logical memory wrapper. Each entry is the full identity of a physical bank.
  std::optional<std::vector<std::string>> memory_banks{};
};
std::vector<Hint_scope> placement_hint_scopes(hhds::Graph&, const physical::Technology&);
struct Hint_object {
  std::string name, master;
  bool        symmetry_x = false, symmetry_y = false;
  std::map<std::string, std::pair<double, double>> signal_pin_centers{};  // DBU, N orientation
};
class Placement_hints {
  struct Hint {
    std::string      id, kind, side, axis, anchor, orientation;
    std::vector<int> subject, reference;
    std::vector<std::string> pins;
    double           x = 0, y = 0, weight = .35;
    std::string      status  = "accepted", reason;
    bool             applied = false;
    int                      columns = 0;
    double                   gap_x = 0, gap_y = 0, anchor_x = .5, anchor_y = .5;
  };
  std::shared_ptr<const metadata::Bundle> input_;
  std::string                             design_;
  std::vector<Hint>                       hints_;
  std::vector<Hint_object>                objects_;
  Centroid_set                            centroids_;
  Centroid_compiled                       centroid_report_;  // kind "centroid": members bind to nodes when a stage compiles them
  double                                  dbu_ = 1;

  std::vector<std::pair<double, double>>  matrix_targets(const Hint&, const DreamPlace::native::Design&, bool* fits_core = nullptr) const;
  std::vector<std::pair<double, double>>  relationship_targets(const Hint&, const DreamPlace::native::Design&) const;
  double pin_distance(const Hint&, const DreamPlace::native::Design&, DreamPlace::native::Orientation) const;
  void                                    reject(Hint&, const std::string&);

public:
  bool has_pull_hints() const;
  // The authored centroid records, scope-qualified; discarded (disabled or rejected) ones are not in the set.
  const Centroid_set& centroids() const { return centroids_; }
  // The per-centroid outcome of the latest compile (members bound, unresolved, strength, layout residual), for the report.
  void note_centroids(Centroid_compiled);
  Placement_hints(std::shared_ptr<const metadata::Bundle>, std::string design, const std::vector<Hint_scope>&,
                  std::vector<Hint_object>, double                     dbu, bool centroids_only = false);
  void                                       rebind(std::vector<Hint_object>);
  void                                       seed(DreamPlace::native::Design&);
  std::vector<DreamPlace::native::MacroPull> pulls(const DreamPlace::native::Design&) const;
  void attach(DreamPlace::native::Options&, std::function<std::vector<Hint_object>()> objects = {}, bool dynamic = false);
  // Output retains every authored/unknown record; generated state is separate.
  rapidjson::Document report(const DreamPlace::native::Design&, bool warnings = false) const;
  rapidjson::Document preview(const DreamPlace::native::Design&) const;
  void append_results(metadata::Bundle&, const DreamPlace::native::Design&);
};
}  // namespace livehd::placement
