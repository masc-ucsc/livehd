//  this file is distributed under the bsd 3-clause license. see license for details.
#pragma once

#include "absl/container/flat_hash_map.h"

#include "cell.hpp"
#include "iassert.hpp"

class Ntype_area {
public:
  struct dim {
    float min_aspect;
    float max_aspect;
    float area;
  };

  Ntype_area() = default;  // don't want to create new area maps all the time

  void       set_dim(Ntype_op op, const dim& d) { type_area_map.insert_or_assign(op, d); }
  [[nodiscard]] const dim& get_dim(Ntype_op op) const {
    I(has_dim(op));
    auto it = type_area_map.find(op);
    I(it != type_area_map.end());
    return it->second;
  };
  [[nodiscard]] bool has_dim(Ntype_op op) const { return type_area_map.contains(op); }
  void clear() { type_area_map.clear(); }

protected:
  absl::flat_hash_map<Ntype_op, dim> type_area_map;
};
