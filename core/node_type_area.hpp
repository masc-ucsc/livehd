#pragma once

#include "cell.hpp"
#include "mmap_map.hpp"
#include "iassert.hpp"

class Ntype_area {
public:
  struct dim {
    float min_aspect;
    float max_aspect;
    float area;
  };

  Ntype_area() = delete;

  static void       set_dim(Ntype_op op, const dim& d) { type_area_map.set(op, d); }
  static const dim& get_dim(Ntype_op op) {
    I(has_dim(op));
    return type_area_map.get(op);
  };
  static bool has_dim(Ntype_op op) { return type_area_map.has(op); }
  static void clear() { type_area_map.clear(); }

protected:
  inline static mmap_lib::map<Ntype_op, dim> type_area_map;
};