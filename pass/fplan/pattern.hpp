#pragma once

#include <functional>  // for std::hash
#include <iostream>
#include <map>
#include <vector>

#include "lgraph_base_core.hpp"

// class representing where the node is located inside of a pattern or the whole floorplan
class Pos {
public:
  double width, height;
  double xpos, ypos;

  Pos() : width(0.0), height(0.0), xpos(0.0), ypos(0.0) {}

  // we may want to create a layout containing the dimensions of the node without knowing where it'll be placed
  Pos(const double nwidth, const double nheight) : width(nwidth), height(nheight) {}

  Pos(const double nwidth, const double nheight, const double nxpos, const double nypos)
      : width(nwidth), height(nheight), xpos(nxpos), ypos(nypos) {}

  friend bool operator==(const Pos& l, const Pos& r) {
    return cmpd(l.width, r.width) && cmpd(l.height, r.height) && cmpd(l.xpos, r.xpos) && cmpd(l.ypos, r.ypos);
  }

private:
  // quick and dirty comparison method
  static constexpr double epsilon = 0.00001;
  static bool             cmpd(const double l, const double r) { return std::abs(l - r) < epsilon; }
};

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

template <>
struct hash<Pos> {
  size_t operator()(const Pos& p) const {
    size_t partial_hash = 0;
    partial_hash ^= std::hash<double>()(p.width);
    partial_hash ^= std::hash<double>()(p.height) << 1;
    partial_hash ^= std::hash<double>()(p.xpos) << 2;
    partial_hash ^= std::hash<double>()(p.ypos) << 3;
    return partial_hash;
  }
};
}  // namespace std