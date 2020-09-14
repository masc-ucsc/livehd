#pragma once

#include <iostream>
#include <unordered_map>
#include <vector>

#include "lgraph_base_core.hpp"

// the dimensions of a module or collection of modules
constexpr double max_aspect_ratio = 1.0 / 5.0;
struct Dim {
  double width;
  double height;

  // for debugging with dag.dump() only
  friend std::ostream& operator<<(std::ostream& os, const Dim& obj) {
    os << "(" << obj.width << ", " << obj.height << ")";
    return os;
  }

  Dim() : width(0.0), height(0.0) {}
  Dim(const double new_width, const double new_height) : width(new_width), height(new_height) {}
};

// a collection of modules
struct Pattern {
  // a list of each kind of module in the pattern, as well as how many modules there are of that type
  std::unordered_map<Lg_type_id::type, unsigned int> verts;

  // dimensions of the pattern
  Dim d;

  friend bool operator==(const Pattern& l, const Pattern& r) { return l.verts == r.verts; }

  Pattern() : verts({}), d() {}
  unsigned int count() const {
    unsigned int total = 0;
    for (auto p : verts) {
      total += p.second;
    }
    return total;
  }
};