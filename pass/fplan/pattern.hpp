#pragma once

#include <functional>  // for std::hash
#include <iostream>
#include <unordered_map>
#include <vector>

#include "lgraph_base_core.hpp"

/*
// class representing the dimensions of a node
class Dim {
public:
  double width;
  double height;
  double xpos;
  double ypos;

  Dim() : width(0.0), height(0.0), xpos(-1.0), ypos(-1.0) {}

  Dim(const double nwidth, const double nheight) : width(nwidth), height(nheight) {}

  friend bool operator==(const Dim& l, const Dim& r) {
    return cmpd(l.width, r.width) && cmpd(l.height, r.height) && cmpd(l.xpos, r.xpos) && cmpd(l.ypos, r.ypos);
  }
  // friend bool operator!=(const Dim& l, const Dim& r) { return !(l == r); }

private:
  static constexpr double epsilon = 0.00001;
  // quick and dirty comparison method
  static bool cmpd(const double l, const double r) { return std::abs(l - r) < epsilon; }
};
*/

// class representing where the node is located inside of a pattern or the whole floorplan
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

// a collection of modules
class Pattern {
public:
  // a list of each kind of module in the pattern, as well as how many modules there are of that type
  std::unordered_map<Lg_type_id::type, unsigned int>        verts;
  std::unordered_map<Lg_type_id::type, std::vector<Layout>> layouts;

  Layout pat_layout;

  // in order to use a Pattern as a key in an unordered_map, we need it to have operator() (for hashing) and operator==
  // (in case the hash ends up being the same by coincidence)
  friend bool operator==(const Pattern& l, const Pattern& r) {
    return l.verts == r.verts && l.pat_layout == r.pat_layout && l.layouts == r.layouts;
  }

  Pattern() : verts(), layouts(), pat_layout() {}

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