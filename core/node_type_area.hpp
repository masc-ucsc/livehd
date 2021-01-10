#pragma once

#include "cell.hpp"
#include "iassert.hpp"
#include "mmap_map.hpp"

class Ntype_area {
public:
  struct dim {
    float min_aspect;
    float max_aspect;
    float area;
  };

  Ntype_area() { abort(); } // TODO: should never run this
  Ntype_area(const std::string_view path, const std::string_view map_name) : type_area_map(path, map_name) {}

  void       set_dim(Ntype_op op, const dim& d) { type_area_map.set(op, d); }
  const dim& get_dim(Ntype_op op) {
    I(has_dim(op));
    return type_area_map.get(op);
  };
  bool has_dim(Ntype_op op) { return type_area_map.has(op); }
  void clear() { type_area_map.clear(); }

protected:
  mmap_lib::map<Ntype_op, dim> type_area_map;
};