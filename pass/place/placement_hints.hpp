// This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#pragma once
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/metadata.hpp"
#include "dreamplace/native/placement.hpp"
#include "hhds/graph.hpp"
#include "inou/lefdef/physical.hpp"
namespace livehd::placement {
// Logical scopes are captured before flattening/releasing the source library.
struct Hint_scope {
  std::string              module;
  std::vector<std::string> path;
};
std::vector<Hint_scope> placement_hint_scopes(hhds::Graph&, const physical::Technology&);
struct Hint_object {
  std::string name, master;
  bool        symmetry_x = false, symmetry_y = false;
};
class Placement_hints {
  struct Hint {
    std::string      id, kind, side, axis, anchor, orientation;
    std::vector<int> subject, reference;
    double           x = 0, y = 0, weight = .35;
    std::string      status  = "accepted", reason;
    bool             applied = false;
  };
  std::shared_ptr<const metadata::Bundle> input_;
  std::string                             design_;
  std::vector<Hint>                       hints_;
  std::vector<Hint_object>                objects_;
  double                                  dbu_ = 1;
  void                                    reject(Hint&, const std::string&);

public:
  Placement_hints(std::shared_ptr<const metadata::Bundle>, std::string design, const std::vector<Hint_scope>&,
                  std::vector<Hint_object>, double                     dbu);
  void                                       rebind(std::vector<Hint_object>);
  void                                       seed(DreamPlace::native::Design&);
  std::vector<DreamPlace::native::MacroPull> pulls(const DreamPlace::native::Design&) const;
  void attach(DreamPlace::native::Options&, std::function<std::vector<Hint_object>()> objects = {});
  // Output retains every authored/unknown record; generated state is separate.
  void write(const DreamPlace::native::Design&, const std::string& path);
};
}  // namespace livehd::placement
