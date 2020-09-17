#pragma once

#include <functional>  // for std::hash
#include <iostream>
#include <map>
#include <vector>

#include "lgraph_base_core.hpp"

// class representing where the node is located inside of a pattern or the whole floorplan
/*
class Layout {
public:
  double width, height;
  double xpos, ypos;

  Layout() : width(0.0), height(0.0), xpos(-1.0), ypos(-1.0) {}

  // we may want to create a layout containing the dimensions of the node without knowing where it'll be placed
  Layout(const double nwidth, const double nheight) : width(nwidth), height(nheight), xpos(-1.0), ypos(-1.0) {}

  friend bool operator==(const Layout& l, const Layout& r) {
    return cmpd(l.width, r.width) && cmpd(l.height, r.height) && cmpd(l.xpos, r.xpos) && cmpd(l.ypos, r.ypos);
  }

private:
  static constexpr double epsilon = 0.00001;
  // quick and dirty comparison method
  static bool cmpd(const double l, const double r) { return std::abs(l - r) < epsilon; }
};
*/

// a collection of modules
class Pattern {
public:
  // a list of each kind of module in the pattern, as well as how many modules there are of that type
  // this needs to be an ordered map since we want patterns that differ in order to be hashed the same way
  std::map<Lg_type_id::type, unsigned int> verts;

  // in order to use a Pattern as a key in an unordered_map, we need it to have operator() (for hashing) and operator==
  // (in case the hash ends up being the same by coincidence)
  friend bool operator==(const Pattern& l, const Pattern& r) { return l.verts == r.verts; }

  Pattern() : verts() {}

  unsigned int count() const {
    unsigned int total = 0;
    for (auto p : verts) {
      total += p.second;
    }
    return total;
  }
};

// the specialization for hashing Patterns needs to be included in the std namespace
namespace std {
template <>
struct hash<Pattern> {
  size_t operator()(const Pattern& p) const {
    size_t i            = 0;
    size_t partial_hash = 0;
    for (auto vpair : p.verts) {
      size_t pair_hash = std::hash<Lg_type_id::type>()(vpair.first) ^ std::hash<unsigned int>()(vpair.second) << 1;
      partial_hash ^= pair_hash << ++i;
    }
    return partial_hash;
  }
};
}  // namespace std